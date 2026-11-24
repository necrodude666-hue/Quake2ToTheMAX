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

#include "gl_local.h"
#include "../client/simple_enemies.h"

image_t		gltextures[MAX_GLTEXTURES];
int			numgltextures;
int			base_textureid;		// gltextures[i] = base_textureid+i

static byte			 intensitytable[256];
static unsigned char gammatable[256];

cvar_t		*intensity;

unsigned	d_8to24table[256];

qboolean GL_Upload8 (byte *data, int width, int height,  qboolean mipmap, qboolean is_sky );
qboolean GL_Upload32 (unsigned *data, int width, int height,  qboolean mipmap);


int		gl_solid_format = 3;
int		gl_alpha_format = 4;

int		gl_tex_solid_format = 3;
int		gl_tex_alpha_format = 4;

int		gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
int		gl_filter_max = GL_LINEAR;

#define GL_TEXTURE_MAX_ANISOTROPY_BDGN 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_BDGN 0x84FF

static GLfloat gl_anisotropy_max;
static GLfloat gl_anisotropy_filter;
static int gl_hd_limit;
static cvar_t *gl_hdtextures;
static qboolean gl_upload_hd;
static int gl_upload_plain;	/* BDGN 3.55ag: the next GL_Upload32 skips the intensity scaling */
static image_t *gl_reload_slot;	/* BDGN 3.56r: GL_LoadPic writes into this image instead of a free one */
static unsigned gl_hd_budget;
#define HD_MIB (1024u*1024u)
/* BDGN 3.55w: compressed HD textures (DDS with DXT1 / DXT5 and mipmaps) for the 4K set (pak8-hd4k-*.pak). The
   driver gets the blocks as they are (glCompressedTexImage2D); levels above gl_hd_maxsize are skipped. */
static void (APIENTRY *qglCompressedTexImage2D_bdgn)(GLenum,GLint,GLenum,GLsizei,GLsizei,GLint,GLsizei,const void *);
static qboolean gl_dds_capable;
static cvar_t *gl_hd_maxsize;
typedef struct { const byte *data; int w,h,levels,format,blockbytes; qboolean sky; float gain; } gl_dds_upload_t;
static gl_dds_upload_t *gl_upload_dds;
static void Q_strncpyz_bdgn(char *dst,const char *src,int size) { strncpy(dst,src,size-1); dst[size-1]=0; }

static unsigned gl_hd_bytes_used;

static void GL_DeleteImageTexture(image_t *image)
{
	int unit;
	for (unit=0;unit<2;unit++)
		if (gl_state.currenttextures[unit]==(int)image->texnum)
			gl_state.currenttextures[unit]=-1;
	qglDeleteTextures(1,&image->texnum);
	gl_hd_bytes_used-=image->hd_bytes;
	memset(image,0,sizeof(*image));
}

static qboolean GL_FilterExtension(const char *extensions, const char *name)
{
	const char *p=extensions;
	size_t len=strlen(name);
	while (p && (p=strstr(p,name))!=NULL) {
		if ((p==extensions || p[-1]==' ') && (p[len]==' ' || !p[len]))
			return true;
		p+=len;
	}
	return false;
}

void GL_InitTextureFiltering(void)
{
	const char *extensions=(const char *)gl_config.extensions_string;
	const char *version=(const char *)gl_config.version_string;
	int major=0,minor=0;
	gl_anisotropy_max=0;
	if (ri.Cvar_Get("bdgn_legacy_graphics","0",0)->value) {
		ri.Cvar_SetValue("bdgn_gl_anisotropy_max",0);
		return;
	}
	if (version) sscanf(version,"%d.%d",&major,&minor);
	if (major>4 || (major==4 && minor>=6) ||
	    GL_FilterExtension(extensions,"GL_EXT_texture_filter_anisotropic") ||
	    GL_FilterExtension(extensions,"GL_ARB_texture_filter_anisotropic"))
		qglGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_BDGN,&gl_anisotropy_max);
	if (!(gl_anisotropy_max>=2.0f)) gl_anisotropy_max=0;
	ri.Cvar_SetValue("bdgn_gl_anisotropy_max",gl_anisotropy_max);
}

static void GL_SetMipmapFilters(void)
{
	qglTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,gl_filter_min);
	qglTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,gl_filter_max);
	if (gl_anisotropy_max>=2.0f)
		qglTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_BDGN,gl_anisotropy_filter);
}

void GL_SetTexturePalette( unsigned palette[256] )
{
	int i;
	unsigned char temptable[768];

	if ( qglColorTableEXT && gl_ext_palettedtexture->value )
	{
		for ( i = 0; i < 256; i++ )
		{
			temptable[i*3+0] = ( palette[i] >> 0 ) & 0xff;
			temptable[i*3+1] = ( palette[i] >> 8 ) & 0xff;
			temptable[i*3+2] = ( palette[i] >> 16 ) & 0xff;
		}

		qglColorTableEXT( GL_SHARED_TEXTURE_PALETTE_EXT,
						   GL_RGB,
						   256,
						   GL_RGB,
						   GL_UNSIGNED_BYTE,
						   temptable );
	}
}

void GL_EnableMultitexture( qboolean enable )
{
	if ( !qglSelectTextureSGIS && !qglActiveTextureARB )
		return;

	if ( enable )
	{
		GL_SelectTexture( GL_TEXTURE1 );
		qglEnable( GL_TEXTURE_2D );
		GL_TexEnv( GL_REPLACE );
	}
	else
	{
		GL_SelectTexture( GL_TEXTURE1 );
		qglDisable( GL_TEXTURE_2D );
		GL_TexEnv( GL_REPLACE );
	}
	GL_SelectTexture( GL_TEXTURE0 );
	GL_TexEnv( GL_REPLACE );
}

void GL_SelectTexture( GLenum texture )
{
	int tmu;

	if ( !qglSelectTextureSGIS && !qglActiveTextureARB )
		return;

	if ( texture == GL_TEXTURE0 )
	{
		tmu = 0;
	}
	else
	{
		tmu = 1;
	}

	if ( tmu == gl_state.currenttmu )
	{
		return;
	}

	gl_state.currenttmu = tmu;

	if ( qglSelectTextureSGIS )
	{
		qglSelectTextureSGIS( texture );
	}
	else if ( qglActiveTextureARB )
	{
		qglActiveTextureARB( texture );
		qglClientActiveTextureARB( texture );
	}
}

void GL_TexEnv( GLenum mode )
{
	static int lastmodes[2] = { -1, -1 };

	if ( mode != lastmodes[gl_state.currenttmu] )
	{
		qglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode );
		lastmodes[gl_state.currenttmu] = mode;
	}
}

void GL_Bind (int texnum)
{
	extern	image_t	*draw_chars;

	if (gl_nobind->value && draw_chars)		// performance evaluation option
		texnum = draw_chars->texnum;
	if ( gl_state.currenttextures[gl_state.currenttmu] == texnum)
		return;
	gl_state.currenttextures[gl_state.currenttmu] = texnum;
	qglBindTexture (GL_TEXTURE_2D, texnum);
}

void GL_MBind( GLenum target, int texnum )
{
	GL_SelectTexture( target );
	if ( target == GL_TEXTURE0 )
	{
		if ( gl_state.currenttextures[0] == texnum )
			return;
	}
	else
	{
		if ( gl_state.currenttextures[1] == texnum )
			return;
	}
	GL_Bind( texnum );
}

typedef struct
{
	char *name;
	int	minimize, maximize;
} glmode_t;

glmode_t modes[] = {
	{"GL_NEAREST", GL_NEAREST, GL_NEAREST},
	{"GL_LINEAR", GL_LINEAR, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}
};

#define NUM_GL_MODES (sizeof(modes) / sizeof (glmode_t))

typedef struct
{
	char *name;
	int mode;
} gltmode_t;

gltmode_t gl_alpha_modes[] = {
	{"default", 4},
	{"GL_RGBA", GL_RGBA},
	{"GL_RGBA8", GL_RGBA8},
	{"GL_RGB5_A1", GL_RGB5_A1},
	{"GL_RGBA4", GL_RGBA4},
	{"GL_RGBA2", GL_RGBA2},
};

#define NUM_GL_ALPHA_MODES (sizeof(gl_alpha_modes) / sizeof (gltmode_t))

gltmode_t gl_solid_modes[] = {
	{"default", 3},
	{"GL_RGB", GL_RGB},
	{"GL_RGB8", GL_RGB8},
	{"GL_RGB5", GL_RGB5},
	{"GL_RGB4", GL_RGB4},
	{"GL_R3_G3_B2", GL_R3_G3_B2},
#ifdef GL_RGB2_EXT
	{"GL_RGB2", GL_RGB2_EXT},
#endif
};

#define NUM_GL_SOLID_MODES (sizeof(gl_solid_modes) / sizeof (gltmode_t))

/*
===============
GL_TextureMode
===============
*/
void GL_TextureMode( char *string )
{
	int		i;
	image_t	*glt;

	for (i=0 ; i< NUM_GL_MODES ; i++)
	{
		if ( !Q_stricmp( modes[i].name, string ) )
			break;
	}

	if (i == NUM_GL_MODES)
	{
		ri.Con_Printf (PRINT_ALL, "bad filter name\n");
		return;
	}

	gl_filter_min = modes[i].minimize;
	gl_filter_max = modes[i].maximize;
	gl_anisotropy_filter=gl_anisotropy->value;
	if (gl_anisotropy_filter>gl_anisotropy_max) gl_anisotropy_filter=gl_anisotropy_max;
	if (!(gl_anisotropy_filter>=1.0f)) gl_anisotropy_filter=1.0f;

	GL_SelectTexture(GL_TEXTURE0);
	// change all the existing mipmap texture objects
	for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
	{
		if (glt->texnum && glt->type != it_pic && glt->type != it_sky && !glt->nearest )	/* BDGN 3.51bct: classic-look sprites keep GL_NEAREST */
		{
			GL_Bind (glt->texnum);
			GL_SetMipmapFilters();
		}
	}
}

/*
===============
GL_TextureAlphaMode
===============
*/
void GL_TextureAlphaMode( char *string )
{
	int		i;

	for (i=0 ; i< NUM_GL_ALPHA_MODES ; i++)
	{
		if ( !Q_stricmp( gl_alpha_modes[i].name, string ) )
			break;
	}

	if (i == NUM_GL_ALPHA_MODES)
	{
		ri.Con_Printf (PRINT_ALL, "bad alpha texture mode name\n");
		return;
	}

	gl_tex_alpha_format = gl_alpha_modes[i].mode;
}

/*
===============
GL_TextureSolidMode
===============
*/
void GL_TextureSolidMode( char *string )
{
	int		i;

	for (i=0 ; i< NUM_GL_SOLID_MODES ; i++)
	{
		if ( !Q_stricmp( gl_solid_modes[i].name, string ) )
			break;
	}

	if (i == NUM_GL_SOLID_MODES)
	{
		ri.Con_Printf (PRINT_ALL, "bad solid texture mode name\n");
		return;
	}

	gl_tex_solid_format = gl_solid_modes[i].mode;
}

/*
===============
GL_ImageList_f
===============
*/
void	GL_ImageList_f (void)
{
	int		i;
	image_t	*image;
	int		texels;
	const char *palstrings[2] =
	{
		"RGB",
		"PAL"
	};

	ri.Con_Printf (PRINT_ALL, "------------------\n");
	texels = 0;

	for (i=0, image=gltextures ; i<numgltextures ; i++, image++)
	{
		if (image->texnum <= 0)
			continue;
		texels += image->upload_width*image->upload_height;
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

		ri.Con_Printf (PRINT_ALL,  " %3i %3i %s: %s\n",
			image->upload_width, image->upload_height, palstrings[image->paletted], image->name);
	}
	ri.Con_Printf (PRINT_ALL, "Total texel count (not counting mipmaps): %i\n", texels);
	if (gl_hd_limit)
		ri.Con_Printf(PRINT_ALL,"HD textures: %.1f / %u MiB estimated, max edge %d\n",
			(double)gl_hd_bytes_used/HD_MIB,gl_hd_budget/HD_MIB,gl_hd_limit);
}


/*
=============================================================================

  scrap allocation

  Allocate all the little status bar obejcts into a single texture
  to crutch up inefficient hardware / drivers

=============================================================================
*/

#define	MAX_SCRAPS		1
#define	BLOCK_WIDTH		256
#define	BLOCK_HEIGHT	256

int			scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte		scrap_texels[MAX_SCRAPS][BLOCK_WIDTH*BLOCK_HEIGHT];
qboolean	scrap_dirty;

// returns a texture number and the position inside it
int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		texnum;

	for (texnum=0 ; texnum<MAX_SCRAPS ; texnum++)
	{
		best = BLOCK_HEIGHT;

		for (i=0 ; i<BLOCK_WIDTH-w ; i++)
		{
			best2 = 0;

			for (j=0 ; j<w ; j++)
			{
				if (scrap_allocated[texnum][i+j] >= best)
					break;
				if (scrap_allocated[texnum][i+j] > best2)
					best2 = scrap_allocated[texnum][i+j];
			}
			if (j == w)
			{	// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i=0 ; i<w ; i++)
			scrap_allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	return -1;
//	Sys_Error ("Scrap_AllocBlock: full");
}

int	scrap_uploads;

void Scrap_Upload (void)
{
	scrap_uploads++;
	GL_Bind(TEXNUM_SCRAPS);
	GL_Upload8 (scrap_texels[0], BLOCK_WIDTH, BLOCK_HEIGHT, false, false );
	// BDGN 3.50: scrap pics are drawn 1:1 or scaled up by a whole number (scr_scale_active): keep their
	// pixels sharp, and keep linear filtering from blending in the neighbouring pics of the atlas
	qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	scrap_dirty = false;
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
	/* Palette output is optional (the animated sprite cache needs pixels only). */
	if (palette)
		*palette = NULL;

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
	byte tmp[2];

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
	
	tmp[0] = buf_p[0];
	tmp[1] = buf_p[1];
	targa_header.colormap_index = LittleShort ( *((short *)tmp) );
	buf_p+=2;
	tmp[0] = buf_p[0];
	tmp[1] = buf_p[1];
	targa_header.colormap_length = LittleShort ( *((short *)tmp) );
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


/*
====================================================================

IMAGE FLOOD FILLING

====================================================================
*/


/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes
=================
*/

typedef struct
{
	short		x, y;
} floodfill_t;

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy ) \
{ \
	if (pos[off] == fillcolor) \
	{ \
		pos[off] = 255; \
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
	} \
	else if (pos[off] != 255) fdc = pos[off]; \
}

void R_FloodFillSkin( byte *skin, int skinwidth, int skinheight )
{
	byte				fillcolor = *skin; // assume this is the pixel to fill
	floodfill_t			fifo[FLOODFILL_FIFO_SIZE];
	int					inpt = 0, outpt = 0;
	int					filledcolor = -1;
	int					i;

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		//printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int			x = fifo[outpt].x, y = fifo[outpt].y;
		int			fdc = filledcolor;
		byte		*pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)				FLOODFILL_STEP( -1, -1, 0 );
		if (x < skinwidth - 1)	FLOODFILL_STEP( 1, 1, 0 );
		if (y > 0)				FLOODFILL_STEP( -skinwidth, 0, -1 );
		if (y < skinheight - 1)	FLOODFILL_STEP( skinwidth, 0, 1 );
		skin[x + skinwidth * y] = fdc;
	}
}

//=======================================================


/*
================
GL_ResampleTexture
================
*/
void GL_ResampleTexture (unsigned *in, int inwidth, int inheight, unsigned *out,  int outwidth, int outheight)
{
	int		i, j;
	unsigned	*inrow, *inrow2;
	unsigned	frac, fracstep;
	unsigned	p1[1024], p2[1024];
	byte		*pix1, *pix2, *pix3, *pix4;

	fracstep = inwidth*0x10000/outwidth;

	frac = fracstep>>2;
	for (i=0 ; i<outwidth ; i++)
	{
		p1[i] = 4*(frac>>16);
		frac += fracstep;
	}
	frac = 3*(fracstep>>2);
	for (i=0 ; i<outwidth ; i++)
	{
		p2[i] = 4*(frac>>16);
		frac += fracstep;
	}

	for (i=0 ; i<outheight ; i++, out += outwidth)
	{
		inrow = in + inwidth*(int)((i+0.25)*inheight/outheight);
		inrow2 = in + inwidth*(int)((i+0.75)*inheight/outheight);
		frac = fracstep >> 1;
		if ( GL_MMX() )
		{
			Q3M_ResampleRow( out, (byte *)inrow, (byte *)inrow2, p1, p2, outwidth );
			continue;
		}
		for (j=0 ; j<outwidth ; j++)
		{
			pix1 = (byte *)inrow + p1[j];
			pix2 = (byte *)inrow + p2[j];
			pix3 = (byte *)inrow2 + p1[j];
			pix4 = (byte *)inrow2 + p2[j];
			((byte *)(out+j))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0])>>2;
			((byte *)(out+j))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1])>>2;
			((byte *)(out+j))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2])>>2;
			((byte *)(out+j))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3])>>2;
		}
	}
}

/*
================
GL_LightScaleTexture

Scale up the pixel values in a texture to increase the
lighting range
================
*/
void GL_LightScaleTexture (unsigned *in, int inwidth, int inheight, qboolean only_gamma )
{
	if ( only_gamma )
	{
		int		i, c;
		byte	*p;

		p = (byte *)in;

		c = inwidth*inheight;
		for (i=0 ; i<c ; i++, p+=4)
		{
			p[0] = gammatable[p[0]];
			p[1] = gammatable[p[1]];
			p[2] = gammatable[p[2]];
		}
	}
	else
	{
		int		i, c;
		byte	*p;

		p = (byte *)in;

		c = inwidth*inheight;
		for (i=0 ; i<c ; i++, p+=4)
		{
			p[0] = gammatable[intensitytable[p[0]]];
			p[1] = gammatable[intensitytable[p[1]]];
			p[2] = gammatable[intensitytable[p[2]]];
		}
	}
}

/*
================
GL_MipMap

Operates in place, quartering the size of the texture
================
*/
void GL_MipMap (byte *in, int width, int height)
{
	int		i, j;
	byte	*out;

	if (width==1 || height==1)
	{
		int n=width>height?width:height;
		for (i=0;i<n/2;i++)
			for (j=0;j<4;j++)
				in[i*4+j]=(in[i*8+j]+in[i*8+4+j])/2;
		return;
	}

	if (Q3S2_TryMipMap(in,width,height)) return;
	if ( GL_MMX() )
	{
		Q3M_MipMap( in, width, height );
		return;
	}

	width <<=2;
	height >>= 1;
	out = in;
	for (i=0 ; i<height ; i++, in+=width)
	{
		for (j=0 ; j<width ; j+=8, out+=4, in+=8)
		{
			out[0] = (in[0] + in[4] + in[width+0] + in[width+4])>>2;
			out[1] = (in[1] + in[5] + in[width+1] + in[width+5])>>2;
			out[2] = (in[2] + in[6] + in[width+2] + in[width+6])>>2;
			out[3] = (in[3] + in[7] + in[width+3] + in[width+7])>>2;
		}
	}
}

/*
===============
GL_Upload32

Returns has_alpha
===============
*/
void GL_BuildPalettedTexture( unsigned char *paletted_texture, unsigned char *scaled, int scaled_width, int scaled_height )
{
	int i;

	for ( i = 0; i < scaled_width * scaled_height; i++ )
	{
		unsigned int r, g, b, c;

		r = ( scaled[0] >> 3 ) & 31;
		g = ( scaled[1] >> 2 ) & 63;
		b = ( scaled[2] >> 3 ) & 31;

		c = r | ( g << 5 ) | ( b << 11 );

		paletted_texture[i] = gl_state.d_16to8table[c];

		scaled += 4;
	}
}

int		upload_width, upload_height;
qboolean uploaded_paletted;

qboolean GL_Upload32 (unsigned *data, int width, int height,  qboolean mipmap)
{
	int			samples;
	unsigned	scaled[256*256];
	unsigned char paletted_texture[256*256];
	int			scaled_width, scaled_height;
	int			i, c;
	byte		*scan;
	int comp;

	uploaded_paletted = false;

	for (scaled_width = 1 ; scaled_width < width ; scaled_width<<=1)
		;
	if (gl_round_down->value && scaled_width > width && mipmap)
		scaled_width >>= 1;
	for (scaled_height = 1 ; scaled_height < height ; scaled_height<<=1)
		;
	if (gl_round_down->value && scaled_height > height && mipmap)
		scaled_height >>= 1;

	// let people sample down the world textures for speed
	if (mipmap)
	{
		scaled_width >>= (int)gl_picmip->value;
		scaled_height >>= (int)gl_picmip->value;
	}

	// BDGN 3.37: gl_maxtexsize = smaller world / skin textures (needs a renderer restart)
	if (mipmap && gl_maxtexsize->value >= 8)
	{
		int maxsize = (int)gl_maxtexsize->value;

		while (scaled_width > maxsize && scaled_width > 1)
			scaled_width >>= 1;
		while (scaled_height > maxsize && scaled_height > 1)
			scaled_height >>= 1;
	}

	// don't ever bother with >256 textures
	if (scaled_width > 256)
		scaled_width = 256;
	if (scaled_height > 256)
		scaled_height = 256;

	if (scaled_width < 1)
		scaled_width = 1;
	if (scaled_height < 1)
		scaled_height = 1;

	upload_width = scaled_width;
	upload_height = scaled_height;

	if (scaled_width * scaled_height > sizeof(scaled)/4)
		ri.Sys_Error (ERR_DROP, "GL_Upload32: too big");

	// scan the texture for any non-255 alpha
	c = width*height;
	scan = ((byte *)data) + 3;
	samples = gl_solid_format;
	for (i=0 ; i<c ; i++, scan += 4)
	{
		if ( *scan != 255 )
		{
			samples = gl_alpha_format;
			break;
		}
	}

	if (samples == gl_solid_format)
	    comp = gl_tex_solid_format;
	else if (samples == gl_alpha_format)
	    comp = gl_tex_alpha_format;
	else {
	    ri.Con_Printf (PRINT_ALL,
			   "Unknown number of texture components %i\n",
			   samples);
	    comp = samples;
	}

#if 0
	if (mipmap)
		gluBuild2DMipmaps (GL_TEXTURE_2D, samples, width, height, GL_RGBA, GL_UNSIGNED_BYTE, trans);
	else if (scaled_width == width && scaled_height == height)
		qglTexImage2D (GL_TEXTURE_2D, 0, comp, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, trans);
	else
	{
		gluScaleImage (GL_RGBA, width, height, GL_UNSIGNED_BYTE, trans,
			scaled_width, scaled_height, GL_UNSIGNED_BYTE, scaled);
		qglTexImage2D (GL_TEXTURE_2D, 0, comp, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
	}
#else

	if (scaled_width == width && scaled_height == height)
	{
		if (!mipmap)
		{
			if ( qglColorTableEXT && gl_ext_palettedtexture->value && samples == gl_solid_format )
			{
				uploaded_paletted = true;
				GL_BuildPalettedTexture( paletted_texture, ( unsigned char * ) data, scaled_width, scaled_height );
				qglTexImage2D( GL_TEXTURE_2D,
							  0,
							  GL_COLOR_INDEX8_EXT,
							  scaled_width,
							  scaled_height,
							  0,
							  GL_COLOR_INDEX,
							  GL_UNSIGNED_BYTE,
							  paletted_texture );
			}
			else
			{
				qglTexImage2D (GL_TEXTURE_2D, 0, comp, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			}
			goto done;
		}
		memcpy (scaled, data, width*height*4);
	}
	else
		GL_ResampleTexture (data, width, height, scaled, scaled_width, scaled_height);

	/* BDGN 3.55ag: gl_upload_plain = this picture holds colours that are drawn as they are (the blood atlas): gamma
	   only, no "intensity" (2 by default: it doubled every colour, so all blood became the same saturated light red) */
	GL_LightScaleTexture (scaled, scaled_width, scaled_height, !mipmap || gl_upload_plain );
	gl_upload_plain = 0;

	if ( qglColorTableEXT && gl_ext_palettedtexture->value && ( samples == gl_solid_format ) )
	{
		uploaded_paletted = true;
		GL_BuildPalettedTexture( paletted_texture, ( unsigned char * ) scaled, scaled_width, scaled_height );
		qglTexImage2D( GL_TEXTURE_2D,
					  0,
					  GL_COLOR_INDEX8_EXT,
					  scaled_width,
					  scaled_height,
					  0,
					  GL_COLOR_INDEX,
					  GL_UNSIGNED_BYTE,
					  paletted_texture );
	}
	else
	{
		qglTexImage2D( GL_TEXTURE_2D, 0, comp, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled );
	}

	if (mipmap)
	{
		int		miplevel;

		miplevel = 0;
		while (scaled_width > 1 || scaled_height > 1)
		{
			GL_MipMap ((byte *)scaled, scaled_width, scaled_height);
			scaled_width >>= 1;
			scaled_height >>= 1;
			if (scaled_width < 1)
				scaled_width = 1;
			if (scaled_height < 1)
				scaled_height = 1;
			miplevel++;
			if ( qglColorTableEXT && gl_ext_palettedtexture->value && samples == gl_solid_format )
			{
				uploaded_paletted = true;
				GL_BuildPalettedTexture( paletted_texture, ( unsigned char * ) scaled, scaled_width, scaled_height );
				qglTexImage2D( GL_TEXTURE_2D,
							  miplevel,
							  GL_COLOR_INDEX8_EXT,
							  scaled_width,
							  scaled_height,
							  0,
							  GL_COLOR_INDEX,
							  GL_UNSIGNED_BYTE,
							  paletted_texture );
			}
			else
			{
				qglTexImage2D (GL_TEXTURE_2D, miplevel, comp, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
			}
		}
	}
done: ;
#endif


	if (mipmap)
	{
		GL_SetMipmapFilters();
	}
	else
	{
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}

	return (samples == gl_alpha_format);
}

/*
===============
GL_Upload8

Returns has_alpha
===============
*/
/*
static qboolean IsPowerOf2( int value )
{
	int i = 1;


	while ( 1 )
	{
		if ( value == i )
			return true;
		if ( i > value )
			return false;
		i <<= 1;
	}
}
*/

qboolean GL_Upload8 (byte *data, int width, int height,  qboolean mipmap, qboolean is_sky )
{
	unsigned	trans[512*256];
	int			i, s;
	int			p;

	s = width*height;

	if (s > sizeof(trans)/4)
		ri.Sys_Error (ERR_DROP, "GL_Upload8: too large");

	if ( qglColorTableEXT && 
		 gl_ext_palettedtexture->value && 
		 is_sky )
	{
		qglTexImage2D( GL_TEXTURE_2D,
					  0,
					  GL_COLOR_INDEX8_EXT,
					  width,
					  height,
					  0,
					  GL_COLOR_INDEX,
					  GL_UNSIGNED_BYTE,
					  data );

		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}
	else
	{
		for (i=0 ; i<s ; i++)
		{
			p = data[i];
			trans[i] = d_8to24table[p];

			if (p == 255)
			{	// transparent, so scan around for another color
				// to avoid alpha fringes
				// FIXME: do a full flood fill so mips work...
				if (i > width && data[i-width] != 255)
					p = data[i-width];
				else if (i < s-width && data[i+width] != 255)
					p = data[i+width];
				else if (i > 0 && data[i-1] != 255)
					p = data[i-1];
				else if (i < s-1 && data[i+1] != 255)
					p = data[i+1];
				else
					p = 0;
				// copy rgb components
				((byte *)&trans[i])[0] = ((byte *)&d_8to24table[p])[0];
				((byte *)&trans[i])[1] = ((byte *)&d_8to24table[p])[1];
				((byte *)&trans[i])[2] = ((byte *)&d_8to24table[p])[2];
			}
		}

		return GL_Upload32 (trans, width, height, mipmap);
	}
	return false;
}


/*
================
GL_LoadPic

This is also used as an entry point for the generated r_notexture
================
*/
/*
=============================================================================
BDGN 3.45: dominant material colour of every wall texture and model skin (r_drawflat 2 = material, 3 = grey,
4 = retro palette).  flatcolors.txt ("name R G B" per line, made by tools/flatcolors_tool.py from the pak files with
colour clustering) is read once; a texture that is not in it (mods, replacement packs) gets the same kind of colour
from its own pixels: the most populated cell of a 8x8x8 colour histogram on about 1000 evenly spread samples.
=============================================================================
*/
#define FLAT_HASH	8192

typedef struct { char *name; byte rgb[3]; } flatent_t;

static flatent_t	*flat_tab;
static char			*flat_text;

static unsigned FlatHash (const char *s)
{
	unsigned h = 5381;

	for ( ; *s ; s++ )
	{
		int c = (unsigned char)*s;

		if ( c >= 'A' && c <= 'Z' )
			c += 32;
		if ( c == '\\' )
			c = '/';
		h = h * 33 + c;
	}
	return h;
}

void GL_FlatLoadDB (void)
{
	char	*buf, *line, *end;
	int		len, n = 0;

	flat_tab = NULL;
	flat_text = NULL;
	len = ri.FS_LoadFile ("flatcolors.txt", (void **)&buf);
	if ( len <= 0 || !buf )
		return;
	flat_text = malloc (len + 1);
	flat_tab = calloc (FLAT_HASH, sizeof(flatent_t));
	if ( !flat_text || !flat_tab )
	{
		ri.FS_FreeFile (buf);
		if ( flat_text ) free (flat_text);
		if ( flat_tab ) free (flat_tab);
		flat_text = NULL;
		flat_tab = NULL;
		return;
	}
	memcpy (flat_text, buf, len);
	flat_text[len] = 0;
	ri.FS_FreeFile (buf);

	for ( line = flat_text ; *line && n < FLAT_HASH * 3 / 4 ; line = end )
	{
		char	*q;
		int		r, g, b;

		end = line;
		while ( *end && *end != '\n' )
			end++;
		if ( *end )
			*end++ = 0;
		if ( *line == '#' || *line == '\r' || !*line )
			continue;
		q = line;
		while ( *q && *q != ' ' && *q != '\t' )
			q++;
		if ( !*q )
			continue;
		*q++ = 0;
		if ( sscanf (q, "%d %d %d", &r, &g, &b) == 3 )
		{
			unsigned h = FlatHash (line) & (FLAT_HASH - 1);

			while ( flat_tab[h].name )
				h = (h + 1) & (FLAT_HASH - 1);
			flat_tab[h].name = line;
			flat_tab[h].rgb[0] = (byte)r;
			flat_tab[h].rgb[1] = (byte)g;
			flat_tab[h].rgb[2] = (byte)b;
			n++;
		}
	}
}

void GL_FlatFreeDB (void)
{
	if ( flat_tab )
		free (flat_tab);
	if ( flat_text )
		free (flat_text);
	flat_tab = NULL;
	flat_text = NULL;
}

static int FlatLookup (const char *name, byte *rgb)
{
	unsigned h;

	if ( !flat_tab )
		return 0;
	h = FlatHash (name) & (FLAT_HASH - 1);
	while ( flat_tab[h].name )
	{
		if ( !Q_stricmp (flat_tab[h].name, name) )
		{
			rgb[0] = flat_tab[h].rgb[0];
			rgb[1] = flat_tab[h].rgb[1];
			rgb[2] = flat_tab[h].rgb[2];
			return 1;
		}
		h = (h + 1) & (FLAT_HASH - 1);
	}
	return 0;
}

void GL_FlatLift (float *c)
{
	float v = c[0];
	float vo;
	int i;

	if ( c[1] > v ) v = c[1];
	if ( c[2] > v ) v = c[2];
	if ( v < 1 ) v = 1;
	vo = 60 + 1.5f * v;
	if ( vo < 80 ) vo = 80;
	if ( vo > 235 ) vo = 235;
	for ( i = 0 ; i < 3 ; i++ )
	{
		c[i] = c[i] * vo / v;
		if ( c[i] > 255 ) c[i] = 255;
	}
}

/* water, slime and lava must be recognisable whatever their textures look like */
static int FlatLiquid (const char *name, byte *rgb)
{
	if ( strstr (name, "lava") )		{ rgb[0] = 235; rgb[1] = 70;  rgb[2] = 25;  return 1; }
	if ( strstr (name, "slime") )		{ rgb[0] = 90;  rgb[1] = 210; rgb[2] = 60;  return 1; }
	if ( strstr (name, "water") )		{ rgb[0] = 80;  rgb[1] = 125; rgb[2] = 235; return 1; }
	return 0;
}

static void FlatEstimate (byte *pic, int width, int height, int bits, byte *rgb)
{
	static int	cnt[512], sum[512][3];
	int			x, y, step, best = -1, bestscore = -1, i;
	float		c[3];

	memset (cnt, 0, sizeof(cnt));
	memset (sum, 0, sizeof(sum));
	step = 1;
	while ( (width / step) * (height / step) > 1400 )
		step++;
	for ( y = 0 ; y < height ; y += step )
		for ( x = 0 ; x < width ; x += step )
		{
			int r, g, b, bin;

			if ( bits == 8 )
			{
				unsigned t = d_8to24table[pic[y * width + x]];

				r = t & 255; g = (t >> 8) & 255; b = (t >> 16) & 255;
			}
			else
			{
				byte *p = pic + (y * width + x) * 4;

				r = p[0]; g = p[1]; b = p[2];
			}
			bin = ((r >> 5) << 6) | ((g >> 5) << 3) | (b >> 5);
			cnt[bin]++;
			sum[bin][0] += r; sum[bin][1] += g; sum[bin][2] += b;
		}
	for ( i = 0 ; i < 512 ; i++ )
	{
		int v, score;

		if ( !cnt[i] )
			continue;
		v = sum[i][0] / cnt[i];
		if ( sum[i][1] / cnt[i] > v ) v = sum[i][1] / cnt[i];
		if ( sum[i][2] / cnt[i] > v ) v = sum[i][2] / cnt[i];
		score = cnt[i] * 10;
		if ( v < 28 )  score = cnt[i] * 3;		/* near black: mortar, shadow */
		if ( v > 238 ) score = cnt[i] * 6;		/* blown out */
		if ( score > bestscore ) { bestscore = score; best = i; }
	}
	if ( best < 0 )
	{
		rgb[0] = rgb[1] = rgb[2] = 128;
		return;
	}
	for ( i = 0 ; i < 3 ; i++ )
		c[i] = (float)sum[best][i] / cnt[best];
	GL_FlatLift (c);
	for ( i = 0 ; i < 3 ; i++ )
		rgb[i] = (byte)(c[i] + 0.5f);
}

static void GL_FlatSetColor (image_t *image, byte *pic, int width, int height, int bits)
{
	image->flat_rgb[0] = image->flat_rgb[1] = image->flat_rgb[2] = 128;
	image->flat_gray = 128;
	if ( image->type != it_wall && image->type != it_skin )
		return;
	if ( !( image->type == it_wall && FlatLiquid (image->name, image->flat_rgb) ) &&
		 !FlatLookup (image->name, image->flat_rgb) )
	{
		if ( pic && width > 0 && height > 0 )
			FlatEstimate (pic, width, height, bits, image->flat_rgb);
	}
	image->flat_gray = (byte)((image->flat_rgb[0] * 77 + image->flat_rgb[1] * 150 + image->flat_rgb[2] * 29) >> 8);
}

void GL_FlatImageColor (image_t *im, int mode, byte *rgb)
{
	if ( !im )
	{
		rgb[0] = rgb[1] = rgb[2] = 128;
		return;
	}
	if ( mode == 3 )
		rgb[0] = rgb[1] = rgb[2] = im->flat_gray;
	else
	{
		rgb[0] = im->flat_rgb[0];
		rgb[1] = im->flat_rgb[1];
		rgb[2] = im->flat_rgb[2];
	}
}

/* the 16 colours of a classic EGA/VGA text screen, without black */
static const byte flat_retro[15][3] =
{
	{ 0, 0, 170 }, { 0, 170, 0 }, { 0, 170, 170 }, { 170, 0, 0 }, { 170, 0, 170 }, { 170, 85, 0 }, { 170, 170, 170 },
	{ 85, 85, 85 }, { 85, 85, 255 }, { 85, 255, 85 }, { 85, 255, 255 }, { 255, 85, 85 }, { 255, 85, 255 }, { 255, 255, 85 },
	{ 255, 255, 255 }
};

void GL_FlatRetro (byte *rgb)
{
	int i, best = 0, bestd = 0x7fffffff;

	for ( i = 0 ; i < 15 ; i++ )
	{
		int dr = rgb[0] - flat_retro[i][0], dg = rgb[1] - flat_retro[i][1], db = rgb[2] - flat_retro[i][2];
		int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;

		if ( d < bestd ) { bestd = d; best = i; }
	}
	rgb[0] = flat_retro[best][0];
	rgb[1] = flat_retro[best][1];
	rgb[2] = flat_retro[best][2];
}

/* Reuse the existing dominant material colour; never replace it for world drawing. */
static int GL_BrightHue (const byte rgb[3])
{
 int r=rgb[0],g=rgb[1],b=rgb[2],mx,mn,d,h;
 mx=r>g?r:g;if(b>mx)mx=b;
 mn=r<g?r:g;if(b<mn)mn=b;d=mx-mn;
 if(d<12) return -1;
 if(mx==r)h=60*(g-b)/d;
 else if(mx==g)h=120+60*(b-r)/d;
 else h=240+60*(r-g)/d;
 if(h<0)h+=360;return h;
}
static void GL_BrightRGB (int hue,int high,int low,byte rgb[3])
{
 int sector,f,p;
 hue=(hue+360)%360;sector=hue/60;f=hue%60;p=(high-low)*f/60;
 switch(sector) {
 case 0:rgb[0]=high;rgb[1]=low+p;rgb[2]=low;break;
 case 1:rgb[0]=high-p;rgb[1]=high;rgb[2]=low;break;
 case 2:rgb[0]=low;rgb[1]=high;rgb[2]=low+p;break;
 case 3:rgb[0]=low;rgb[1]=high-p;rgb[2]=high;break;
 case 4:rgb[0]=low+p;rgb[1]=low;rgb[2]=high;break;
 default:rgb[0]=high;rgb[1]=low;rgb[2]=high-p;break;
 }
}
void GL_BrightSkinColor (image_t *image,byte rgb[3])
{
 static const int hues[6]={120,30,230,285,60,0};
 static byte occupied[8192];
 unsigned int hash=5381;const char *s;
 int i,j,hue,base=0,best=361,d,attempt,code,key,family;
 if(!image) {rgb[0]=0;rgb[1]=255;rgb[2]=0;return;}
 /* Damaged skins share the healthy variant's cached bright colour. */
 for(i=0;i<sizeof(simple_enemies)/sizeof(simple_enemies[0]);i++)
  if(!strcmp(image->name,simple_enemies[i].skin) && strcmp(simple_enemies[i].skin,simple_enemies[i].bright_skin)) {
   image_t *healthy = GL_FindImage((char *)simple_enemies[i].bright_skin,it_skin);
   if(healthy) {GL_BrightSkinColor(healthy,rgb);return;}
   break;
  }

 if(!image->bright_valid) {
  memset(occupied,0,sizeof(occupied));
  for(j=0;j<numgltextures;j++)if(&gltextures[j]!=image && gltextures[j].texnum && gltextures[j].bright_valid) {
   key=((gltextures[j].bright_rgb[0]>>3)<<11)|((gltextures[j].bright_rgb[1]>>2)<<5)|(gltextures[j].bright_rgb[2]>>3);
   occupied[key>>3]|=1<<(key&7);
  }
  for(s=image->name;*s;s++)hash=hash*33+(unsigned char)*s;
  hue=GL_BrightHue(image->flat_rgb);
  if(hue<0)base=hash%6;
  else for(i=0;i<6;i++) {
   d=abs(hue-hues[i]);if(d>180)d=360-d;
   if(d<best){best=d;base=i;}
  }
  /* Saturated families supply 1266 distinct RGB565 colours for <=1024 images. */
  for(attempt=0;attempt<=6*4352;attempt++) {
   if(!attempt)GL_BrightRGB(hues[base],255,0,rgb);
   else {
    code=(hash%4352+attempt-1)%4352;
    family=(base+(attempt-1)/4352)%6;
    GL_BrightRGB(hues[family]+code%17-8,240+(code/272)%16,((code/17)%16)*3,rgb);
   }
   key=((rgb[0]>>3)<<11)|((rgb[1]>>2)<<5)|(rgb[2]>>3);
   if(!(occupied[key>>3]&(1<<(key&7)))) {
    /* Store a canonical 16-bit colour so 16/32-bit modes remain distinguishable. */
    rgb[0]=((key>>11)&31)*255/31;rgb[1]=((key>>5)&63)*255/63;rgb[2]=(key&31)*255/31;
    break;
   }
  }
  memcpy(image->bright_rgb,rgb,3);image->bright_valid=true;
 }
 memcpy(rgb,image->bright_rgb,3);
}

/* HD assets are already power-of-two RGBA and own their decoded pixel buffer. */
static qboolean GL_UploadHD(byte *pixels,int width,int height,qboolean mipmap)
{
	int level=0,i;
	qboolean alpha=false;
	for(i=0;i<width*height;i++) if(pixels[i*4+3]!=255) { alpha=true; break; }
	upload_width=width;
	upload_height=height;
	uploaded_paletted=false;
	GL_LightScaleTexture((unsigned *)pixels,width,height,!mipmap);
	for (;;) {
		qglTexImage2D(GL_TEXTURE_2D,level++,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
		if (!mipmap || (width==1 && height==1)) break;
		if ((width>1 && (width&1)) || (height>1 && (height&1))) {
			int x,y,c, nextw=width>1?width/2:1, nexth=height>1?height/2:1;
			for (y=0;y<nexth;y++) for (x=0;x<nextw;x++) {
				int a=4*(2*y*width+2*x);
				int b=a+(width>1?4:0), stride=height>1?4*width:0;
				for (c=0;c<4;c++)
					pixels[4*(y*nextw+x)+c]=(pixels[a+c]+pixels[b+c]+pixels[a+stride+c]+pixels[b+stride+c])/4;
			}
		} else GL_MipMap(pixels,width,height);
		if (width>1) width/=2;
		if (height>1) height/=2;
	}
	if (mipmap) GL_SetMipmapFilters();
	else {
		qglTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,gl_filter_max);
		qglTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,gl_filter_max);
	}
	return alpha;
}

/* BDGN 3.55w: the levels of a DDS (already cut to the size limit) to the bound texture; returns has_alpha */
/* Match the RGBA HD upload's intensity and gamma without expanding the DDS.
   Keep the DXT1 endpoint ordering, which controls its palette mode. */
/* Match the replacement against the corresponding original before the common gamma/intensity
   transfer. Sample across the whole texture and leave transparent texels out of the comparison. */
static float GL_HDImageLuma(const byte *pixels,int width,int height,int bits)
{
	int x,y,xstep=width/32+1,ystep=height/32+1;
	unsigned sum=0,weight=0;
	for (y=ystep/2;y<height;y+=ystep)
		for (x=xstep/2;x<width;x+=xstep) {
			int i=y*width+x;
			const byte *rgba;
			if (bits==8) rgba=(const byte *)&d_8to24table[pixels[i]];
			else rgba=pixels+i*4;
			if ((bits==8 && pixels[i]==255) || rgba[3]<128) continue;
			sum+=54*rgba[0]+183*rgba[1]+19*rgba[2];
			weight+=256;
		}
	return weight ? (float)sum/weight : -1;
}

static float GL_HDLumaGain(float original,float replacement)
{
	float gain;
	if (original<0 || replacement<1) return 1;
	gain=original/replacement;
	if (gain<0.5f) return 0.5f;
	if (gain>3.0f) return 3.0f;
	return gain;
}

static int GL_HDScaleChannel(int channel,float gain)
{
	int result=(int)(channel*gain+0.5f);
	return result>255 ? 255 : result;
}

static int GL_DDSColorLuma(unsigned rgb)
{
	return 54*((rgb>>11)&31)*255/31+183*((rgb>>5)&63)*255/63+19*(rgb&31)*255/31;
}

static float GL_DDSImageLuma(const gl_dds_upload_t *d)
{
	int x,y,xstep=d->w/32+1,ystep=d->h/32+1,blocks=(d->w+3)/4;
	unsigned sum=0,weight=0;
	for (y=ystep/2;y<d->h;y+=ystep)
		for (x=xstep/2;x<d->w;x+=xstep) {
			const byte *b=d->data+((y/4)*blocks+x/4)*d->blockbytes;
			int pixel=(y&3)*4+(x&3), index, a,c,luma_a,luma_c;
			if (d->blockbytes==16) {
				int bit=3*pixel, k=2+bit/8;
				int alpha_index=((b[k]|(b[k+1]<<8))>>(bit&7))&7;
				int alpha;
				if (alpha_index<2) alpha=b[alpha_index];
				else if (b[0]>b[1]) alpha=((8-alpha_index)*b[0]+(alpha_index-1)*b[1])/7;
				else if (alpha_index<6) alpha=((6-alpha_index)*b[0]+(alpha_index-1)*b[1])/5;
				else alpha=alpha_index==7 ? 255 : 0;
				if (alpha<128) continue;
				b+=8;
			}
			a=b[0]|(b[1]<<8); c=b[2]|(b[3]<<8);
			index=(b[4+(y&3)]>>(2*(x&3)))&3;
			luma_a=GL_DDSColorLuma(a); luma_c=GL_DDSColorLuma(c);
			if (index==0) sum+=luma_a;
			else if (index==1) sum+=luma_c;
			else if (d->blockbytes==16 || a>c)
				sum+=index==2 ? (2*luma_a+luma_c)/3 : (luma_a+2*luma_c)/3;
			else if (index==2) sum+=(luma_a+luma_c)/2;
			/* DXT1 RGB decodes index 3 of a three-colour block as black. */
			weight+=256;
		}
	return weight ? (float)sum/weight : -1;
}

static unsigned GL_DDSChannel(int value,int max,qboolean sky,float gain)
{
	int channel=GL_HDScaleChannel(value*255/max,gain);
	if (!sky) channel=intensitytable[channel];
	return (gammatable[channel]*max+127)/255;
}

static qboolean GL_UploadDDS (gl_dds_upload_t *d)
{
	int i, w=d->w, h=d->h, levels=d->sky ? 1 : d->levels;
	const byte *p=d->data;
	unsigned table5[32], table6[64];
	byte *blocks=malloc(((w+3)/4)*((h+3)/4)*d->blockbytes);
	if (!blocks) { d->levels=0; return false; }
	for (i=0;i<32;i++) table5[i]=GL_DDSChannel(i,31,d->sky || gl_upload_plain,d->gain);	/* 3.56r: plain = no intensity (blood) */
	for (i=0;i<64;i++) table6[i]=GL_DDSChannel(i,63,d->sky || gl_upload_plain,d->gain)<<5;
	for (i=0;i<levels;i++) {
		int size=((w+3)/4)*((h+3)/4)*d->blockbytes;
		int j;
		memcpy(blocks,p,size);
		for (j=d->blockbytes==16?8:0;j<size;j+=d->blockbytes) {
			byte *b=blocks+j;
			unsigned a=b[0]|(b[1]<<8), c=b[2]|(b[3]<<8);
			unsigned aa=(table5[a>>11]<<11)|table6[(a>>5)&63]|table5[a&31];
			unsigned cc=(table5[c>>11]<<11)|table6[(c>>5)&63]|table5[c&31];
			if (d->blockbytes==8 && ((a>c && aa<cc) || (a<=c && aa>cc))) {
				unsigned swap=aa;
				int k;
				aa=cc; cc=swap;
				for (k=4;k<8;k++) {
					unsigned indices=b[k];
					/* Swap 0/1 and, in four-color blocks, 2/3. */
					b[k]=(byte)(indices ^ (a>c ? 0x55u : ((~indices>>1)&0x55u)));
				}
			}
			if (d->blockbytes==8 && a>c && aa==cc) {
				if (aa<65535) aa++;
				else cc--;
			}
			b[0]=(byte)aa; b[1]=(byte)(aa>>8);
			b[2]=(byte)cc; b[3]=(byte)(cc>>8);
		}
		qglCompressedTexImage2D_bdgn(GL_TEXTURE_2D,i,d->format,w,h,0,size,blocks);
		p+=size;
		if (w==1 && h==1) { levels=i+1; break; }
		if (w>1) w/=2;
		if (h>1) h/=2;
	}
	if (levels>1) {
		qglTexParameteri(GL_TEXTURE_2D,0x813D /* GL_TEXTURE_MAX_LEVEL */,levels-1);
		GL_SetMipmapFilters();
	} else {
		qglTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,gl_filter_max);
		qglTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,gl_filter_max);
	}
	free(blocks);
	upload_width=d->w; upload_height=d->h;
	uploaded_paletted=false;
	return d->format!=0x83F0;
}

image_t *GL_LoadPic (char *name, byte *pic, int width, int height, imagetype_t type, int bits)
{
	image_t		*image;
	int			i;

	// find a free image_t
	for (i=0, image=gltextures ; i<numgltextures ; i++,image++)
	{
		if (!image->texnum)
			break;
	}
	if (gl_reload_slot)
		i = gl_reload_slot - gltextures;	/* BDGN 3.56r: reloaded in place (GL_FX4KCheck), same texture name */
	else if (i == numgltextures)
	{
		if (numgltextures == MAX_GLTEXTURES)
			ri.Sys_Error (ERR_DROP, "MAX_GLTEXTURES");
		numgltextures++;
	}
	image = &gltextures[i];

	if (strlen(name) >= sizeof(image->name))
		ri.Sys_Error (ERR_DROP, "Draw_LoadPic: \"%s\" is too long", name);
	strcpy (image->name, name);
	image->registration_sequence = registration_sequence;

	image->width = width;
	image->height = height;
	image->type = type;
	image->bright_valid = false;
	GL_FlatSetColor (image, pic, width, height, bits);		// BDGN 3.45

	if (type == it_skin && bits == 8)
		R_FloodFillSkin(pic, width, height);

	// load little pics into the scrap
	if (image->type == it_pic && bits == 8
		&& image->width < 64 && image->height < 64)
	{
		int		x, y;
		int		i, j, k;
		int		texnum;

		texnum = Scrap_AllocBlock (image->width, image->height, &x, &y);
		if (texnum == -1)
			goto nonscrap;
		scrap_dirty = true;

		// copy the texels into the scrap block
		k = 0;
		for (i=0 ; i<image->height ; i++)
			for (j=0 ; j<image->width ; j++, k++)
				scrap_texels[texnum][(y+i)*BLOCK_WIDTH + x + j] = pic[k];
		image->texnum = TEXNUM_SCRAPS + texnum;
		image->scrap = true;
		image->nearest = false;
		image->has_alpha = true;
		image->sl = (x+0.01)/(float)BLOCK_WIDTH;
		image->sh = (x+image->width-0.01)/(float)BLOCK_WIDTH;
		image->tl = (y+0.01)/(float)BLOCK_WIDTH;
		image->th = (y+image->height-0.01)/(float)BLOCK_WIDTH;
	}
	else
	{
nonscrap:
		image->scrap = false;
		image->texnum = TEXNUM_IMAGES + (image - gltextures);
		GL_Bind(image->texnum);
		/* BDGN 3.51bct: the classic-look flat 2D enemies (sprites/enemies/*_c.pcx, *_cm.pcx; simple 2d enemies
		   "low-res") are drawn like classic 2d sprites mode's 32x32 tiles: no mipmaps, no picmip, GL_NEAREST */
		{
			int len = strlen( name );
			/* 3.60i hotfix: picmip 3/4 reduces a thin ground-weapon sprite
			   to transparent texels. Keep pickup silhouettes at their native size,
			   as for low-res enemies; this also prevents distant mip levels vanishing. */
			image->nearest = image->type == it_sprite &&
				(!strncmp(name,"sprites/items/",14) ||
				 (!strncmp(name,"sprites/enemies/",16) &&
				  ((len>6 && !strcmp(name+len-6,"_c.pcx")) || (len>7 && !strcmp(name+len-7,"_cm.pcx")))));
		}
		gl_upload_plain = !strncmp( name, "models/hdgore/decals_blood", 26 ) ||
			!strcmp(name,"models/hdgore/actor_marks_lit.tga");	/* BDGN 3.55ag */
		if (gl_upload_dds) {
			image->has_alpha=GL_UploadDDS(gl_upload_dds);
		} else if (gl_upload_hd) {
			image->has_alpha=GL_UploadHD(pic,width,height,type!=it_sky);
		} else if (bits == 8)
			image->has_alpha = GL_Upload8 (pic, width, height, (image->type != it_pic && image->type != it_sky) && !image->nearest, image->type == it_sky );
		else
			image->has_alpha = GL_Upload32 ((unsigned *)pic, width, height, (image->type != it_pic && image->type != it_sky) && !image->nearest );
		gl_upload_plain = 0;
		if ( image->nearest )
		{
			qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}
		image->upload_width = upload_width;		// after power of 2 and scales
		image->upload_height = upload_height;
		if (image->type == it_pic && upload_width == width && upload_height == height)	// BDGN 3.50: see Scrap_Upload
			qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		image->paletted = uploaded_paletted;
		image->sl = 0;
		image->sh = 1;
		image->tl = 0;
		image->th = 1;
	}

	return image;
}


/*
================
GL_LoadWal
================
*/
/*
================
GL_LoadDDSImage

BDGN 3.55w: textures_hd/<name>.dds (DXT1 opaque, DXT5 with alpha, full mip chain). The largest levels are skipped
until the picture fits gl_hd_limit and gl_hd_maxsize (the "4K texture size" row: 4096 / 2048 / 1024), so a smaller
size costs no extra files. The memory counted against the HD budget is the compressed size.
================
*/
static image_t *GL_LoadDDSImage(char *name,const char *hdpath,int width,int height,imagetype_t type,float original_luma)
{
	char path[MAX_QPATH];
	byte *file=NULL;
	image_t *image=NULL;
	gl_dds_upload_t d;
	int len,w,h,levels,fourcc,i,limit,skip,max_levels;
	unsigned bytes=0,used;
	const byte *p;
	byte flat[4];

	Q_strncpyz_bdgn(path,hdpath,sizeof(path));
	strcpy(path+strlen(path)-4,".dds");
	len=ri.FS_LoadFile(path,(void **)&file);
	if (!file) return NULL;
	if (len<128 || memcmp(file,"DDS ",4)) goto done;
	h=file[12]|(file[13]<<8)|(file[14]<<16)|(file[15]<<24);
	w=file[16]|(file[17]<<8)|(file[18]<<16)|(file[19]<<24);
	levels=file[28]|(file[29]<<8);
	if (levels<1) levels=1;
	fourcc=file[84]|(file[85]<<8)|(file[86]<<16)|(file[87]<<24);
	if (fourcc==('D'|('X'<<8)|('T'<<16)|('1'<<24))) { d.format=0x83F0; d.blockbytes=8; }
	else if (fourcc==('D'|('X'<<8)|('T'<<16)|('5'<<24))) { d.format=0x83F3; d.blockbytes=16; }
	else goto done;
	if (w<width || h<height || w>16384 || h>16384 ||
	    (double)abs(w*height-h*width)*32.0>(double)w*height) goto done;	/* same shape (sizes are rounded to blocks of 4) */
	max_levels=1;
	for (i=w>h?w:h;i>1;i>>=1) max_levels++;
	if (levels>max_levels) goto done;
	limit=gl_hd_limit;
	if (gl_hd_maxsize->value>=256 && gl_hd_maxsize->value<limit) limit=(int)gl_hd_maxsize->value;
	p=file+128;
	for (skip=0;(w>limit || h>limit) && skip<levels-1;skip++) {	/* drop levels above the limit */
		unsigned skipped=((w+3)/4)*((h+3)/4)*d.blockbytes;
		if (skipped>(unsigned)(file+len-p)) goto done;
		p+=skipped;
		if (w>1) w/=2;
		if (h>1) h/=2;
	}
	if (w>limit || h>limit || w<width || h<height) goto done;
	levels-=skip;
	{	/* size check of the remaining levels against the file */
		int x=w,y=h;
		for (i=0;i<levels;i++) {
			bytes+=((x+3)/4)*((y+3)/4)*d.blockbytes;
			if (type==it_sky) break;
			if (x>1) x/=2;
			if (y>1) y/=2;
		}
	}
	if (bytes>(unsigned)(file+len-p)) goto done;
	/* BDGN 3.57d: when the budget is short the picture is not dropped (back to the original 1997 texture) but
	   loaded one or more mip levels smaller, never below the original size. The world (walls, sky) may use at most
	   three quarters of the budget, so the view weapon, the enemies, the effects and the menu pictures, which are
	   loaded after the map, still get their HD versions. Before 3.57d a map with many 4K walls used the whole budget
	   and everything after it was drawn with the original textures. */
	{
		unsigned budget=gl_hd_budget;
		if (type==it_wall || type==it_sky) budget-=gl_hd_budget/4;
		used=gl_hd_bytes_used;
		while (used<=budget && bytes>budget-used && type!=it_sky && levels>1 && w/2>=width && h/2>=height) {
			unsigned first=((w+3)/4)*((h+3)/4)*d.blockbytes;
			p+=first; bytes-=first; levels--;
			if (w>1) w/=2;
			if (h>1) h/=2;
		}
		if (used>budget || bytes>budget-used) goto done;
	}
	/* a flat colour for r_drawflat: colour 0 of the first block of the last level */
	{
		const byte *b=p;
		int x=w,y=h,c;
		for (i=0;type!=it_sky && i<levels-1;i++) { b+=((x+3)/4)*((y+3)/4)*d.blockbytes; if (x>1) x/=2; if (y>1) y/=2; }
		if (d.blockbytes==16) b+=8;
		c=b[0]|(b[1]<<8);
		flat[0]=(byte)(((c>>11)&31)*255/31); flat[1]=(byte)(((c>>5)&63)*255/63); flat[2]=(byte)((c&31)*255/31); flat[3]=255;
	}
	d.data=p; d.w=w; d.h=h; d.levels=levels; d.sky=type==it_sky;
	d.gain=GL_HDLumaGain(original_luma,GL_DDSImageLuma(&d));
	for(i=0;i<16 && qglGetError()!=GL_NO_ERROR;i++) {}
	gl_upload_dds=&d;
	image=GL_LoadPic(name,flat,1,1,type,32);
	gl_upload_dds=NULL;
	if (!d.levels || qglGetError()!=GL_NO_ERROR) {
		GL_DeleteImageTexture(image);
		image=NULL;
	} else {
		image->width=width;
		image->height=height;
		image->hd_bytes=bytes;
		gl_hd_bytes_used+=bytes;
	}
done:
	ri.FS_FreeFile(file);
	return image;
}

static image_t *GL_LoadHDImage(char *name,int width,int height,imagetype_t type,const byte *original,int bits)
{
	char path[MAX_QPATH];
	byte *file=NULL,*pixels;
	image_t *image;
	int len,w,h,i,x,y,offset;
	unsigned bytes, used;
	float original_luma,gain;

	if (!gl_hd_limit || !gl_hdtextures->value || gl_nobind->value || ri.Cvar_Get("fs_lowtextures","0",0)->value ||
	    strlen(name)<4) return NULL;
	if (type==it_wall && !strncmp(name,"textures/",9))
	{
		if (strlen(name)+3>=sizeof(path)) return NULL;
		Com_sprintf(path,sizeof(path),"textures_hd/%s",name+9);
	}
	else if ((type==it_skin && (!strncmp(name,"models/",7) || !strncmp(name,"players/",8))) ||
	         (type==it_sprite && !strncmp(name,"sprites/",8)) ||
	         (type==it_sky && !strncmp(name,"env/",4)) ||
	         (type==it_pic && !strncmp(name,"pics/",5)))	/* BDGN 3.56r: HUD, menus and console font of the HD sets */
	{
		if (strlen(name)+12>=sizeof(path)) return NULL;
		Com_sprintf(path,sizeof(path),"textures_hd/%s",name);
	}
	else return NULL;
	original_luma=GL_HDImageLuma(original,width,height,bits);
	if (gl_dds_capable && (image=GL_LoadDDSImage(name,path,width,height,type,original_luma))!=NULL)
		return image;	/* BDGN 3.55w: 4K set */
	strcpy(path+strlen(path)-4,".tga");
	len=ri.FS_LoadFile(path,(void **)&file);
	if (!file) return NULL;
	image=NULL;
	if (len<18 || file[1] || file[2]!=2 || file[16]!=32 || (file[17]&0xd0)) goto done;
	w=file[12]+256*file[13];
	h=file[14]+256*file[15];
	offset=18+file[0];
	if (w<width || h<height || w>gl_hd_limit || h>gl_hd_limit ||
	    abs(w*height-h*width)>(width>height?width:height) ||
	    offset>len || w*h>(len-offset)/4) goto done;
	/* Count the actual RGBA8 mip chain, including rectangular tails. */
	bytes=0;
	for (x=w,y=h;;) {
		bytes+=4u*x*y;
		if (type==it_sky || (x==1 && y==1)) break;
		if (x>1) x/=2;
		if (y>1) y/=2;
	}
	used=gl_hd_bytes_used;
	{	/* BDGN 3.57d: the world may use three quarters of the budget (models, effects and menus come after it) */
		unsigned budget=gl_hd_budget;
		if (type==it_wall || type==it_sky) budget-=gl_hd_budget/4;
		if (used>budget || bytes>budget-used) goto done;
	}
	pixels=malloc(w*h*4);
	if (!pixels) goto done;
	for(y=0;y<h;y++) {
		const byte *src=file+offset+(file[17]&32?y:h-1-y)*w*4;
		byte *out=pixels+y*w*4;
		for(x=0;x<w;x++,src+=4,out+=4) {
			out[0]=src[2]; out[1]=src[1]; out[2]=src[0]; out[3]=src[3];
		}
	}
	for(i=0;i<16 && qglGetError()!=GL_NO_ERROR;i++) {}
	gain=GL_HDLumaGain(original_luma,GL_HDImageLuma(pixels,w,h,32));
	if (gain<0.99f || gain>1.01f) {
		byte table[256];
		for(i=0;i<256;i++) table[i]=(byte)GL_HDScaleChannel(i,gain);
		for(i=0;i<w*h*4;i+=4) {
			pixels[i]=table[pixels[i]];
			pixels[i+1]=table[pixels[i+1]];
			pixels[i+2]=table[pixels[i+2]];
		}
	}
	gl_upload_hd=true;
	image=GL_LoadPic(name,pixels,w,h,type,32);
	gl_upload_hd=false;
	free(pixels);
	if (qglGetError()!=GL_NO_ERROR) {
		GL_DeleteImageTexture(image);
		image=NULL;
	} else {
		image->width=width;
		image->height=height;
		image->hd_bytes=bytes;
		gl_hd_bytes_used+=bytes;
	}
done:
	ri.FS_FreeFile(file);
	return image;
}

image_t *GL_LoadWal (char *name)
{
	miptex_t	*mt;
	int			width, height, ofs, length, scale, marker;
	image_t		*image;

	length=ri.FS_LoadFile(name,(void **)&mt);
	if (!mt)
	{
		ri.Con_Printf (PRINT_ALL, "GL_FindImage: can't load %s\n", name);
		return r_notexture;
	}

	if (length<(int)sizeof(*mt)) goto invalid;
	width = LittleLong (mt->width);
	height = LittleLong (mt->height);
	ofs = LittleLong (mt->offsets[0]);
	if (width<=0 || height<=0 || ofs<(int)sizeof(*mt) || ofs>length ||
	    height>(length-ofs)/width || height>(512*256)/width) goto invalid;

	marker = LittleLong(mt->value);
	scale = marker == 0x32484442 ? 4 : marker == 0x31484442 ? 2 : 1;
	if (scale == 1 && width<=1024 && height<=1024) {
		image=GL_LoadHDImage(name,width,height,it_wall,(const byte *)mt+ofs,8);
		if (image) {
			ri.FS_FreeFile(mt);
			return image;
		}
	}
	image = GL_LoadPic (name, (byte *)mt + ofs, width, height, it_wall, 8);
	if (scale != 1 && image != r_notexture)
	{
		image->width = width * scale;
		image->height = height * scale;
	}

	ri.FS_FreeFile ((void *)mt);

	return image;

invalid:
	ri.Con_Printf(PRINT_ALL,"Invalid WAL texture: %s\n",name);
	ri.FS_FreeFile(mt);
	return r_notexture;
}

/*
===============
GL_LoadFX4K

BDGN 3.56r: the HD 4K choices of the effect rows. An effect texture (sprites/hdfx/, models/hdgore/, models/hdfx3d/,
models/hdfx3d_sd/) is a TGA in its HD pack; the 4K pack holds a DDS under the same name, and the file system only
searches that pack while its row is on HD 4K (qcommon/files.c FS_FX4KEnabled). So: when the DDS is found it is used,
with the TGA's size as the picture's size. Needs S3TC (the same path as the AI 4K world set, gl_hd_maxsize applies).
===============
*/
static qboolean GL_IsFXName (const char *name)
{
	return !strncmp (name, "sprites/hdfx/", 13) || !strncmp (name, "models/hdgore/", 14) ||
		!strncmp (name, "models/hdfx3d/", 14) || !strncmp (name, "models/hdfx3d_sd/", 17);
}

static image_t *GL_LoadFX4K (char *name, int width, int height, imagetype_t type, const byte *pic)
{
	image_t *image;
	if (!gl_dds_capable || !gl_hd_limit || !GL_IsFXName (name))
		return NULL;
	gl_upload_plain = !strncmp (name, "models/hdgore/decals_blood", 26) || !strcmp (name, "models/hdgore/actor_marks_lit.tga");
	image = GL_LoadDDSImage (name, name, width, height, type, GL_HDImageLuma (pic, width, height, 32));
	gl_upload_plain = 0;
	return image;
}

/* reload one effect texture in place (same image_t and texture name, so models and sprites keep pointing at it) */
static void GL_ReloadFXImage (image_t *image)
{
	char name[MAX_QPATH];
	imagetype_t type = image->type;
	int seq = image->registration_sequence, width, height;
	byte *pic = NULL;
	image_t *n;
	Q_strncpyz_bdgn (name, image->name, sizeof(name));
	LoadTGA (name, &pic, &width, &height);
	if (!pic)
		return;
	gl_hd_bytes_used -= image->hd_bytes;
	image->hd_bytes = 0;
	gl_reload_slot = image;
	n = GL_LoadFX4K (name, width, height, type, pic);
	if (!n)
	{	/* GL_LoadDDSImage may have emptied the slot on a failed upload: write the TGA into it again */
		gl_reload_slot = image;
		n = GL_LoadPic (name, pic, width, height, type, 32);
	}
	gl_reload_slot = NULL;
	free (pic);
	if (n)
		n->registration_sequence = seq;
}

/* once a frame: when one of the rows changed between HD and HD 4K, the loaded effect textures are reloaded */
void GL_FX4KCheck (void)
{
	static cvar_t *hdfx, *decals, *fx3dtex;
	static int last = -1;
	int key, i;
	image_t *image;
	if (!hdfx)
	{
		hdfx = ri.Cvar_Get ("cl_hdfx", "0", CVAR_ARCHIVE);
		decals = ri.Cvar_Get ("cl_decals", "0", CVAR_ARCHIVE);
		fx3dtex = ri.Cvar_Get ("cl_fx3d_tex", "0", CVAR_ARCHIVE);
	}
	key = (hdfx->value == 2) | ((decals->value == 2) << 1) | ((int)fx3dtex->value << 2);
	if (key == last)
		return;
	if (last < 0)
	{
		last = key;
		return;
	}
	last = key;
	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
	{
		size_t len = strlen (image->name);
		if (image->texnum && !image->scrap && len > 4 && !strcmp (image->name + len - 4, ".tga") && GL_IsFXName (image->name))
			GL_ReloadFXImage (image);
	}
}

/*
===============
GL_FindImage

Finds or loads the given image
===============
*/
image_t	*GL_FindImage (char *name, imagetype_t type)
{
	image_t	*image;
	int		i, len;
	byte	*pic, *palette;
	int		width, height;

	if (!name)
		return NULL;	//	ri.Sys_Error (ERR_DROP, "GL_FindImage: NULL name");
	len = strlen(name);
	if (len<5)
		return NULL;	//	ri.Sys_Error (ERR_DROP, "GL_FindImage: bad name: %s", name);

	// look for it
	for (i=0, image=gltextures ; i<numgltextures ; i++,image++)
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
	{	/* BDGN 3.59q: images read from disk since the start, for the client's load phases (bdgn_mapload.txt):
		   shows whether a map load uploads its sprites again or finds them loaded */
		static int newimages;
		ri.Cvar_SetValue ("bdgn_img_new", (float)++newimages);
	}
	pic = NULL;
	palette = NULL;
	if (!strcmp(name+len-4, ".pcx"))
	{
		LoadPCX (name, &pic, &palette, &width, &height);
		if (!pic)
			return NULL; // ri.Sys_Error (ERR_DROP, "GL_FindImage: can't load %s", name);
		image = GL_LoadHDImage(name,width,height,type,pic,8);
		if (!image) image = GL_LoadPic (name, pic, width, height, type, 8);
	}
	else if (!strcmp(name+len-4, ".wal"))
	{
		// doogies code
		//image = GL_LoadWal (name);
		if (gl_hack_noimages->value)    // Q2TTM: if gl_hack_noimages is "1", apply the "r_notexture" texture to all wal files and do not load from disk
		{
			image = r_notexture;
		}
		else
		{
			image = GL_LoadWal (name);
		}
	}
	else if (!strcmp(name+len-4, ".tga"))
	{
		//LoadTGA (name, &pic, &width, &height);
		//if (!pic)
		//	return NULL; // ri.Sys_Error (ERR_DROP, "GL_FindImage: can't load %s", name);
		//image = GL_LoadPic (name, pic, width, height, type, 32);
		// end
		if (gl_hack_noimages->value)    // Q2TTM: if gl_hack_noimages is "1", apply the "r_notexture" texture to all tga files and do not load from disk
		{
			image = r_notexture;
		}
		else
		{
			LoadTGA (name, &pic, &width, &height);
			if (!pic)
				return NULL; // ri.Sys_Error (ERR_DROP, "GL_FindImage: can't load %s", name);
			image = GL_LoadHDImage(name,width,height,type,pic,32);
			if (!image) image = GL_LoadFX4K (name, width, height, type, pic);	/* BDGN 3.56r */
			if (!image) image = GL_LoadPic (name, pic, width, height, type, 32);
		}
		// end
	}
	else
		return NULL;	//	ri.Sys_Error (ERR_DROP, "GL_FindImage: bad extension on: %s", name);


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
	return GL_FindImage (name, it_skin);
}


/*
================
GL_FreeUnusedImages

Any image that was not touched on this registration sequence
will be freed.
================
*/
void GL_FreeUnusedImages (void)
{
	int		i;
	image_t	*image;

	// never free r_notexture or particle texture
	r_notexture->registration_sequence = registration_sequence;
	r_particletexture->registration_sequence = registration_sequence;

	for (i=0, image=gltextures ; i<numgltextures ; i++, image++)
	{
		if (image->registration_sequence == registration_sequence)
			continue;		// used this sequence
		if (!image->registration_sequence)
			continue;		// free image_t slot
		if (image->type == it_pic)
			continue;		// don't free pics
		// free it
		GL_DeleteImageTexture(image);
	}
}


/*
===============
Draw_GetPalette
===============
*/
int Draw_GetPalette (void)
{
	int		i;
	int		r, g, b;
	unsigned	v;
	byte	*pic, *pal;
	int		width, height;

	// get the palette

	LoadPCX ("pics/colormap.pcx", &pic, &pal, &width, &height);
	if (!pal)
		ri.Sys_Error (ERR_FATAL, "Couldn't load pics/colormap.pcx");

	for (i=0 ; i<256 ; i++)
	{
		r = pal[i*3+0];
		g = pal[i*3+1];
		b = pal[i*3+2];
		
		v = (255<<24) + (r<<0) + (g<<8) + (b<<16);
		d_8to24table[i] = LittleLong(v);
	}

	d_8to24table[255] &= LittleLong(0xffffff);	// 255 is transparent

	free (pic);
	free (pal);

	return 0;
}


/*
===============
GL_InitImages
===============
*/
void	GL_InitImages (void)
{
	int		i, j;
	float	g = vid_gamma->value;

	gl_hdtextures=ri.Cvar_Get("gl_hdtextures","0",CVAR_ARCHIVE);
	gl_hd_limit=0;
	gl_hd_budget=0;
	if (!ri.Cvar_Get("bdgn_legacy_graphics","0",0)->value &&
	    (!Q_stricmp(gl_driver->string,"opengl32") || !Q_stricmp(gl_driver->string,"opengl32.dll")) &&
	    !ri.Cvar_Get("vid_rendition","0",0)->value && qglGetIntegerv && qglGetError &&
	    gl_config.version_string && gl_config.version_string[0]>='2') {
		int ram=(int)ri.Cvar_Get("bdgn_detected_ram_mb","0",CVAR_NOSET)->value;
		qboolean nt=ri.Cvar_Get("bdgn_nt_memory","0",CVAR_NOSET)->value!=0;
		int max_mb=nt && ram>=2048 ? 512 : nt && ram>=1024 ? 256 : 128;
		int max_edge=nt && ram>=2048 ? 4096 : nt && ram>=1024 ? 2048 : 1024;
		float mb=ri.Cvar_Get("gl_hd_budget_mb","0",CVAR_ARCHIVE)->value;

		/* BDGN 3.55w: the 4K set is compressed (DXT); it gets a larger budget on machines with 4 GB or more */
		if ((int)gl_hdtextures->value==5 && nt && ram>=4096) max_mb=768;
		/* BDGN 3.57d: quake2.exe is large address aware now: on 64-bit Windows it has 4 GB of address space instead
		   of 2, so with 8 GB of RAM or more the HD sets get twice the budget (4K set 1536 MB, the others 1024 MB) */
		{
			MEMORYSTATUS ms;
			memset(&ms,0,sizeof(ms));
			ms.dwLength=sizeof(ms);
			GlobalMemoryStatus(&ms);
			if (nt && ram>=8192 && ms.dwTotalVirtual>0xC0000000u)
				max_mb=(int)gl_hdtextures->value==5 ? 1536 : 1024;
		}

		if (!(mb>=16)) mb=(float)max_mb;
		if (mb>max_mb) mb=(float)max_mb;
		gl_hd_budget=(unsigned)mb*HD_MIB;
		qglGetIntegerv(GL_MAX_TEXTURE_SIZE,&gl_hd_limit);
		if (gl_hd_limit<1024) gl_hd_limit=0;
		if (gl_hd_limit>max_edge) gl_hd_limit=max_edge;
	}
	ri.Cvar_SetValue("bdgn_gl_hdtextures_capable",gl_hd_limit!=0);
	/* BDGN 3.55w: DDS / DXT for the 4K set (needs the HD path above and S3TC) */
	gl_hd_maxsize=ri.Cvar_Get("gl_hd_maxsize","4096",CVAR_ARCHIVE);
	gl_dds_capable=false;
	qglCompressedTexImage2D_bdgn=NULL;
#ifdef _WIN32
	if (gl_hd_limit && gl_config.extensions_string && strstr(gl_config.extensions_string,"GL_EXT_texture_compression_s3tc") && qwglGetProcAddress) {
		PROC proc=qwglGetProcAddress("glCompressedTexImage2D");
		if ((size_t)proc<=3 || proc==(PROC)-1) proc=qwglGetProcAddress("glCompressedTexImage2DARB");
		if ((size_t)proc>3 && proc!=(PROC)-1) {
			memcpy(&qglCompressedTexImage2D_bdgn,&proc,sizeof(proc));
			gl_dds_capable=true;
		}
	}
#endif
	ri.Cvar_SetValue("bdgn_gl_dds_capable",gl_dds_capable);
	registration_sequence = 1;

	// init intensity conversions
	intensity = ri.Cvar_Get ("intensity", "2", 0);

	if ( intensity->value <= 1 )
		ri.Cvar_Set( "intensity", "1" );

	gl_state.inverse_intensity = 1 / intensity->value;

	Draw_GetPalette ();
	GL_FlatLoadDB ();		// BDGN 3.45

	if ( qglColorTableEXT )
	{
		ri.FS_LoadFile( "pics/16to8.dat", &gl_state.d_16to8table );
		if ( !gl_state.d_16to8table )
			ri.Sys_Error( ERR_FATAL, "Couldn't load pics/16to8.pcx");
	}

	if ( gl_config.renderer & ( GL_RENDERER_VOODOO | GL_RENDERER_VOODOO2 ) )
	{
		g = 1.0F;
	}

	for ( i = 0; i < 256; i++ )
	{
		if ( g == 1 )
		{
			gammatable[i] = i;
		}
		else
		{
			float inf;

			inf = 255 * pow ( (i+0.5)/255.5 , g ) + 0.5;
			if (inf < 0)
				inf = 0;
			if (inf > 255)
				inf = 255;
			gammatable[i] = inf;
		}
	}

	for (i=0 ; i<256 ; i++)
	{
		j = i*intensity->value;
		if (j > 255)
			j = 255;
		intensitytable[i] = j;
	}
}

/*
===============
GL_ShutdownImages
===============
*/
void	GL_ShutdownImages (void)
{
	int		i;
	image_t	*image;

	GL_FlatFreeDB ();		// BDGN 3.45

	for (i=0, image=gltextures ; i<numgltextures ; i++, image++)
	{
		if (!image->registration_sequence)
			continue;		// free image_t slot
		// free it
		GL_DeleteImageTexture(image);
	}
}


/* 512 shared tiles in small 256x256 atlas pages: 16 tiles of 64x64 per page (32 pages, at most 8 MB RGBA),
   BDGN 3.51q: or 64 tiles of 32x32 per page (8 pages, at most 2 MB). The tile size is fixed until GL_DukeDelete. */
static qboolean duke_atlas[32];
static int duke_atlas_tile;
void GL_DukeDelete(void)
{
 int i;unsigned tex;
 for(i=0;i<32;i++)if(duke_atlas[i]){tex=TEXNUM_DUKE+i;if(qglDeleteTextures)qglDeleteTextures(1,&tex);if(gl_state.currenttextures[0]==tex)gl_state.currenttextures[0]=-1;if(gl_state.currenttextures[1]==tex)gl_state.currenttextures[1]=-1;duke_atlas[i]=false;}
 duke_atlas_tile=0;
}
int GL_DukeAtlasBytes(void)
{
 int i,n=0;for(i=0;i<32;i++)if(duke_atlas[i])n++;return n*256*256*4;
}
void GL_DukeTile(int slot,byte *pixels,image_t *image,int t)
{
 static unsigned rgba[64*64], empty_atlas[256*256];int i,p,row=256/t,per=row*row,page=slot/per,x=(slot%row)*t,y=((slot%per)/row)*t;
 int tex=TEXNUM_DUKE+page;
 if(t!=32 && t!=64)return;
 if(page<0 || page>=32)return;	/* BDGN 3.51att: 32 atlas pages (r_duke_cache is capped to them) */
 if(duke_atlas_tile!=t){GL_DukeDelete();duke_atlas_tile=t;}	/* pages hold one tile size */
 GL_SelectTexture(GL_TEXTURE0);GL_Bind(tex);
 if(!duke_atlas[page]){
  qglTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,256,256,0,GL_RGBA,GL_UNSIGNED_BYTE,empty_atlas);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);duke_atlas[page]=true;
 }
 (void)p;
 {	/* BDGN 3.52 (WESTBECK): gamma applied to the 256 palette colours, then one pass (was: palette pass + gamma pass per tile) */
  unsigned pal[256];for(i=0;i<256;i++)pal[i]=d_8to24table[i];GL_LightScaleTexture(pal,256,1,true);pal[255]&=0x00ffffff;
  for(i=0;i<t*t;i++)rgba[i]=pal[pixels[i]];
 }
 qglTexSubImage2D(GL_TEXTURE_2D,0,x,y,t,t,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
 image->texnum=tex;image->sl=x/256.0f;image->tl=y/256.0f;image->sh=(x+t)/256.0f;image->th=(y+t)/256.0f;image->has_alpha=true;
}
