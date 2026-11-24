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
/*
** RW_DIB.C
**
** This handles DIB section management under Windows.
**
*/
#include "../r_local.h"
#include "rw_win.h"

#ifndef _WIN32
#  error You should not be trying to compile this file on this platform
#endif

static qboolean s_systemcolors_saved;

static HGDIOBJ previously_selected_GDI_obj;

/* BDGN 3.58b: own windowed upscale (sw_upscale). The owner's i7 (3.58a, 1920x1080 window): sw_render_scale 75 % was
   11.7 % SLOWER than 100 % while 50 % was 67 % faster; Windows' StretchBlt of the 8-bit DIB takes a slow path for a
   factor that is not a whole number. Here the frame is widened into a second 8-bit DIB of the window's size with a
   column table (nearest pixel, the same picture as StretchBlt's COLORONCOLOR) and rows that repeat are copied whole;
   then a 1:1 BitBlt, the path a native frame takes. */
static HDC		s_up_dc;
static HBITMAP	s_up_bmp;
static HGDIOBJ	s_up_old;
static byte		*s_up_base;
static int		s_up_w, s_up_h, s_up_srcw, s_up_srch;
static int		*s_up_xmap;

static void DIB_UpscaleFree (void)
{
	if (s_up_dc)
	{
		if (s_up_old) SelectObject (s_up_dc, s_up_old);
		DeleteDC (s_up_dc);
	}
	if (s_up_bmp) DeleteObject (s_up_bmp);
	if (s_up_xmap) free (s_up_xmap);
	s_up_dc = NULL; s_up_bmp = NULL; s_up_old = NULL; s_up_base = NULL; s_up_xmap = NULL;
	s_up_w = s_up_h = s_up_srcw = s_up_srch = 0;
}

static qboolean DIB_UpscaleCreate (int outw, int outh)
{
	struct { BITMAPINFOHEADER h; RGBQUAD c[256]; } bi;
	int x;
	DIB_UpscaleFree ();
	if (outw <= 0 || outh <= 0 || !sww_state.hdcDIBSection)
		return false;
	memset (&bi, 0, sizeof(bi));
	bi.h.biSize = sizeof(BITMAPINFOHEADER);
	bi.h.biWidth = outw;
	bi.h.biHeight = -outh;		/* top down: row y at s_up_base + y * pitch */
	bi.h.biPlanes = 1;
	bi.h.biBitCount = 8;
	bi.h.biCompression = BI_RGB;
	bi.h.biClrUsed = bi.h.biClrImportant = 256;
	GetDIBColorTable (sww_state.hdcDIBSection, 0, 256, bi.c);
	s_up_bmp = CreateDIBSection (sww_state.hDC, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&s_up_base, NULL, 0);
	if (!s_up_bmp || !s_up_base || !(s_up_dc = CreateCompatibleDC (sww_state.hDC))
		|| !(s_up_old = SelectObject (s_up_dc, s_up_bmp)) || !(s_up_xmap = malloc (outw * sizeof(int))))
	{
		DIB_UpscaleFree ();
		return false;
	}
	s_up_w = outw; s_up_h = outh; s_up_srcw = vid.width; s_up_srch = vid.height;
	for (x = 0 ; x < outw ; x++)
		s_up_xmap[x] = x * vid.width / outw;
	return true;
}

/* true = presented; false = use StretchBlt */
qboolean DIB_UpscalePresent (int outw, int outh)
{
	int pitch = (outw + 3) & ~3, y, x, prev = -1;
	if (s_up_w != outw || s_up_h != outh || s_up_srcw != vid.width || s_up_srch != vid.height || !s_up_dc)
		if (!DIB_UpscaleCreate (outw, outh))
			return false;
	for (y = 0 ; y < outh ; y++)
	{
		int sy = y * vid.height / outh;
		byte *dst = s_up_base + y * pitch;
		if (sy == prev)
		{
			memcpy (dst, dst - pitch, outw);
			continue;
		}
		{
			/* the game's DIB is bottom up: screen row sy is (height-1-sy) rows from its base */
			const byte *src = sww_state.pDIBBase + (vid.height - 1 - sy) * vid.width;
			const int *m = s_up_xmap;
			for (x = 0 ; x + 4 <= outw ; x += 4)
			{
				dst[x] = src[m[x]]; dst[x+1] = src[m[x+1]]; dst[x+2] = src[m[x+2]]; dst[x+3] = src[m[x+3]];
			}
			for ( ; x < outw ; x++)
				dst[x] = src[m[x]];
		}
		prev = sy;
	}
	return BitBlt (sww_state.hDC, 0, 0, outw, outh, s_up_dc, 0, 0, SRCCOPY) != 0;
}

static int s_syspalindices[] = 
{
  COLOR_ACTIVEBORDER,
  COLOR_ACTIVECAPTION,
  COLOR_APPWORKSPACE,
  COLOR_BACKGROUND,
  COLOR_BTNFACE,
  COLOR_BTNSHADOW,
  COLOR_BTNTEXT,
  COLOR_CAPTIONTEXT,
  COLOR_GRAYTEXT,
  COLOR_HIGHLIGHT,
  COLOR_HIGHLIGHTTEXT,
  COLOR_INACTIVEBORDER,

  COLOR_INACTIVECAPTION,
  COLOR_MENU,
  COLOR_MENUTEXT,
  COLOR_SCROLLBAR,
  COLOR_WINDOW,
  COLOR_WINDOWFRAME,
  COLOR_WINDOWTEXT
};

#define NUM_SYS_COLORS ( sizeof( s_syspalindices ) / sizeof( int ) )

static int s_oldsyscolors[NUM_SYS_COLORS];

typedef struct dibinfo
{
	BITMAPINFOHEADER	header;
	RGBQUAD				acolors[256];
} dibinfo_t;

typedef struct
{
	WORD palVersion;
	WORD palNumEntries;
	PALETTEENTRY palEntries[256];
} identitypalette_t;

static identitypalette_t s_ipal;

static void DIB_SaveSystemColors( void );
static void DIB_RestoreSystemColors( void );

/*
** DIB_Init
**
** Builds our DIB section
*/
qboolean DIB_Init( unsigned char **ppbuffer, int *ppitch )
{
	dibinfo_t   dibheader;
	BITMAPINFO *pbmiDIB = ( BITMAPINFO * ) &dibheader;
	int i;

	memset( &dibheader, 0, sizeof( dibheader ) );

	/*
	** grab a DC
	*/
	if ( !sww_state.hDC )
	{
		if ( ( sww_state.hDC = GetDC( sww_state.hWnd ) ) == NULL )
			return false;
	}

	/*
	** figure out if we're running in an 8-bit display mode
	*/
 	if ( GetDeviceCaps( sww_state.hDC, RASTERCAPS ) & RC_PALETTE )
	{
		sww_state.palettized = true;

		// save system colors
		if ( !s_systemcolors_saved )
		{
			DIB_SaveSystemColors();
			s_systemcolors_saved = true;
		}
	}
	else
	{
		sww_state.palettized = false;
	}

	/*
	** fill in the BITMAPINFO struct
	*/
	pbmiDIB->bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
	pbmiDIB->bmiHeader.biWidth         = vid.width;
	pbmiDIB->bmiHeader.biHeight        = vid.height;
	pbmiDIB->bmiHeader.biPlanes        = 1;
	pbmiDIB->bmiHeader.biBitCount      = 8;
	pbmiDIB->bmiHeader.biCompression   = BI_RGB;
	pbmiDIB->bmiHeader.biSizeImage     = 0;
	pbmiDIB->bmiHeader.biXPelsPerMeter = 0;
	pbmiDIB->bmiHeader.biYPelsPerMeter = 0;
	pbmiDIB->bmiHeader.biClrUsed       = 256;
	pbmiDIB->bmiHeader.biClrImportant  = 256;

	/*
	** fill in the palette
	*/
	for ( i = 0; i < 256; i++ )
	{
		dibheader.acolors[i].rgbRed   = ( d_8to24table[i] >> 0 )  & 0xff;
		dibheader.acolors[i].rgbGreen = ( d_8to24table[i] >> 8 )  & 0xff;
		dibheader.acolors[i].rgbBlue  = ( d_8to24table[i] >> 16 ) & 0xff;
	}

	/*
	** create the DIB section
	*/
	sww_state.hDIBSection = CreateDIBSection( sww_state.hDC,
		                                     pbmiDIB,
											 DIB_RGB_COLORS,
											 (void **)&sww_state.pDIBBase,
											 NULL,
											 0 );

	if ( sww_state.hDIBSection == NULL )
	{
		ri.Con_Printf( PRINT_ALL, "DIB_Init() - CreateDIBSection failed\n" );
		goto fail;
	}

	if ( pbmiDIB->bmiHeader.biHeight > 0 )
    {
		// bottom up
		*ppbuffer	= sww_state.pDIBBase + ( vid.height - 1 ) * vid.width;
		*ppitch		= -vid.width;
    }
    else
    {
		// top down
		*ppbuffer	= sww_state.pDIBBase;
		*ppitch		= vid.width;
    }

	/*
	** clear the DIB memory buffer
	*/
	memset( sww_state.pDIBBase, 0xff, vid.width * vid.height );

	if ( ( sww_state.hdcDIBSection = CreateCompatibleDC( sww_state.hDC ) ) == NULL )
	{
		ri.Con_Printf( PRINT_ALL, "DIB_Init() - CreateCompatibleDC failed\n" );
		goto fail;
	}
	if ( ( previously_selected_GDI_obj = SelectObject( sww_state.hdcDIBSection, sww_state.hDIBSection ) ) == NULL )
	{
		ri.Con_Printf( PRINT_ALL, "DIB_Init() - SelectObject failed\n" );
		goto fail;
	}

	return true;

fail:
	DIB_Shutdown();
	return false;
	
}

/*
** DIB_SetPalette
**
** Sets the color table in our DIB section, and also sets the system palette
** into an identity mode if we're running in an 8-bit palettized display mode.
**
** The palette is expected to be 1024 bytes, in the format:
**
** R = offset 0
** G = offset 1
** B = offset 2
** A = offset 3
*/
void DIB_SetPalette( const unsigned char *_pal )
{
	const unsigned char *pal = _pal;
  	LOGPALETTE		*pLogPal = ( LOGPALETTE * ) &s_ipal;
	RGBQUAD			colors[256];
	int				i;
	int				ret;
	HDC				hDC = sww_state.hDC;

	/*
	** set the DIB color table
	*/
	if ( sww_state.hdcDIBSection )
	{
		for ( i = 0; i < 256; i++, pal += 4 )
		{
			colors[i].rgbRed   = pal[0];
			colors[i].rgbGreen = pal[1];
			colors[i].rgbBlue  = pal[2];
			colors[i].rgbReserved = 0;
		}

		colors[0].rgbRed = 0;
		colors[0].rgbGreen = 0;
		colors[0].rgbBlue = 0;

		colors[255].rgbRed = 0xff;
		colors[255].rgbGreen = 0xff;
		colors[255].rgbBlue = 0xff;

		if ( SetDIBColorTable( sww_state.hdcDIBSection, 0, 256, colors ) == 0 )
		{
			ri.Con_Printf( PRINT_ALL, "DIB_SetPalette() - SetDIBColorTable failed\n" );
		}
		if ( s_up_dc )
			SetDIBColorTable( s_up_dc, 0, 256, colors );	/* BDGN 3.58b: the upscale DIB has the same colours */
	}

	/*
	** for 8-bit color desktop modes we set up the palette for maximum
	** speed by going into an identity palette mode.
	*/
	if ( sww_state.palettized )
	{
		int i;
		HPALETTE hpalOld;

		if ( SetSystemPaletteUse( hDC, SYSPAL_NOSTATIC ) == SYSPAL_ERROR )
		{
			ri.Sys_Error( ERR_FATAL, "DIB_SetPalette() - SetSystemPaletteUse() failed\n" );
		}

		/*
		** destroy our old palette
		*/
		if ( sww_state.hPal )
		{
			DeleteObject( sww_state.hPal );
			sww_state.hPal = 0;
		}

		/*
		** take up all physical palette entries to flush out anything that's currently
		** in the palette
		*/
		pLogPal->palVersion		= 0x300;
		pLogPal->palNumEntries	= 256;

		for ( i = 0, pal = _pal; i < 256; i++, pal += 4 )
		{
			pLogPal->palPalEntry[i].peRed	= pal[0];
			pLogPal->palPalEntry[i].peGreen	= pal[1];
			pLogPal->palPalEntry[i].peBlue	= pal[2];
			pLogPal->palPalEntry[i].peFlags	= PC_RESERVED | PC_NOCOLLAPSE;
		}
		pLogPal->palPalEntry[0].peRed		= 0;
		pLogPal->palPalEntry[0].peGreen		= 0;
		pLogPal->palPalEntry[0].peBlue		= 0;
		pLogPal->palPalEntry[0].peFlags		= 0;
		pLogPal->palPalEntry[255].peRed		= 0xff;
		pLogPal->palPalEntry[255].peGreen	= 0xff;
		pLogPal->palPalEntry[255].peBlue	= 0xff;
		pLogPal->palPalEntry[255].peFlags	= 0;

		if ( ( sww_state.hPal = CreatePalette( pLogPal ) ) == NULL )
		{
			ri.Sys_Error( ERR_FATAL, "DIB_SetPalette() - CreatePalette failed(%x)\n", GetLastError() );
		}

		if ( ( hpalOld = SelectPalette( hDC, sww_state.hPal, FALSE ) ) == NULL )
		{
			ri.Sys_Error( ERR_FATAL, "DIB_SetPalette() - SelectPalette failed(%x)\n",GetLastError() );
		}

		if ( sww_state.hpalOld == NULL )
			sww_state.hpalOld = hpalOld;

		if ( ( ret = RealizePalette( hDC ) ) != pLogPal->palNumEntries ) 
		{
			ri.Sys_Error( ERR_FATAL, "DIB_SetPalette() - RealizePalette set %d entries\n", ret );
		}
	}
}

/*
** DIB_Shutdown
*/
void DIB_Shutdown( void )
{
	DIB_UpscaleFree ();	/* BDGN 3.58b */
	if ( sww_state.palettized && s_systemcolors_saved )
		DIB_RestoreSystemColors();

	if ( sww_state.hPal )
	{
		DeleteObject( sww_state.hPal );
		sww_state.hPal = 0;
	}

	if ( sww_state.hpalOld )
	{
		SelectPalette( sww_state.hDC, sww_state.hpalOld, FALSE );
		RealizePalette( sww_state.hDC );
		sww_state.hpalOld = NULL;
	}

	if ( sww_state.hdcDIBSection )
	{
		SelectObject( sww_state.hdcDIBSection, previously_selected_GDI_obj );
		DeleteDC( sww_state.hdcDIBSection );
		sww_state.hdcDIBSection = NULL;
	}

	if ( sww_state.hDIBSection )
	{
		DeleteObject( sww_state.hDIBSection );
		sww_state.hDIBSection = NULL;
		sww_state.pDIBBase = NULL;
	}

	if ( sww_state.hDC )
	{
		ReleaseDC( sww_state.hWnd, sww_state.hDC );
		sww_state.hDC = 0;
	}
}


/*
** DIB_Save/RestoreSystemColors
*/
static void DIB_RestoreSystemColors( void )
{
    SetSystemPaletteUse( sww_state.hDC, SYSPAL_STATIC );
    SetSysColors( NUM_SYS_COLORS, s_syspalindices, (const COLORREF *)s_oldsyscolors );
}

static void DIB_SaveSystemColors( void )
{
	int i;

	for ( i = 0; i < NUM_SYS_COLORS; i++ )
		s_oldsyscolors[i] = GetSysColor( s_syspalindices[i] );
}
