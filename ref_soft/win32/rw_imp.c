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
** RW_IMP.C
**
** This file contains ALL Win32 specific stuff having to do with the
** software refresh.  When a port is being made the following functions
** must be implemented by the port:
**
** SWimp_EndFrame
** SWimp_Init
** SWimp_SetPalette
** SWimp_Shutdown
*/
#include "../r_local.h"
#include "rw_win.h"
#include "../../client/render_scale.h"
#include "winquake.h"

// Console variables that we need to access from this module

swwstate_t sww_state;
static int sw_output_width, sw_output_height;
static byte *sw_lost_buffer;
static byte *sw_menu_dib;
static LPDIRECTDRAWSURFACE sw_menu_surface;
static qboolean sw_menu_drawing,sw_menu_ready;
static struct { int width,height,pitch; byte *buffer; } sw_menu_scene;

void SWimp_MenuBegin(void);
void SWimp_MenuEnd(void);

static void SWimp_MenuFallback(void)
{
	if (sw_render_scale->value != 100)
	{
		ri.Con_Printf(PRINT_ALL, "Native Software menu unavailable. Restoring native render size.\n");
		ri.Cvar_SetValue("sw_render_scale", 100);
	}
}

static void SWimp_MenuError(HRESULT result)
{
	switch (result)
	{
	case DD_OK:
	case DDERR_SURFACELOST:
	case DDERR_NOEXCLUSIVEMODE:
	case DDERR_WRONGMODE:
	case DDERR_SURFACEBUSY:
	case DDERR_WASSTILLDRAWING:
		return;
	default:
		SWimp_MenuFallback();
	}
}

/*
** VID_CreateWindow
*/
#define	WINDOW_CLASS_NAME "Quake 2"

void VID_CreateWindow( int width, int height, int stylebits )
{
	WNDCLASS		wc;
	RECT			r;
	cvar_t			*vid_xpos, *vid_ypos, *vid_fullscreen;
	int				x, y, w, h;
	int				exstyle;

	vid_xpos = ri.Cvar_Get ("vid_xpos", "0", 0);
	vid_ypos = ri.Cvar_Get ("vid_ypos", "0", 0);
	vid_fullscreen = ri.Cvar_Get ("vid_fullscreen", "0", CVAR_ARCHIVE );

	if ( vid_fullscreen->value )
		exstyle = WS_EX_TOPMOST;
	else
		exstyle = 0;

	/* Register the frame class */
    wc.style         = 0;
    wc.lpfnWndProc   = (WNDPROC)sww_state.wndproc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = sww_state.hInstance;
    wc.hIcon         = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (101));	/* BDGN 3.57e: the Quake II icon of quake2.exe (win32/q2.rc IDI_ICON1) */
    wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
	wc.hbrBackground = (void *)COLOR_GRAYTEXT;
    wc.lpszMenuName  = 0;
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClass (&wc) )
		ri.Sys_Error (ERR_FATAL, "Couldn't register window class");

	r.left = 0;
	r.top = 0;
	r.right  = width;
	r.bottom = height;

	AdjustWindowRect (&r, stylebits, FALSE);

	w = r.right - r.left;
	h = r.bottom - r.top;
	x = vid_xpos->value;
	y = vid_ypos->value;

	sww_state.hWnd = CreateWindowEx (
		exstyle,
		 WINDOW_CLASS_NAME,
		 "Quake 2",
		 stylebits,
		 x, y, w, h,
		 NULL,
		 NULL,
		 sww_state.hInstance,
		 NULL);

	if (!sww_state.hWnd)
		ri.Sys_Error (ERR_FATAL, "Couldn't create window");
	{	/* BDGN 3.57e: the window and taskbar icon (big and small); before 3.57e the window had none, so Windows
		   showed an empty icon in the taskbar and the title bar */
		HINSTANCE exe = GetModuleHandle (NULL);
		HICON big = LoadIcon (exe, MAKEINTRESOURCE (101));
		HICON small_icon = (HICON)LoadImage (exe, MAKEINTRESOURCE (101), IMAGE_ICON, 16, 16, 0);
		if (big) SendMessage (sww_state.hWnd, WM_SETICON, ICON_BIG, (LPARAM)big);
		if (small_icon) SendMessage (sww_state.hWnd, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
	}
	
	ShowWindow( sww_state.hWnd, SW_SHOWNORMAL );
	UpdateWindow( sww_state.hWnd );
	SetForegroundWindow( sww_state.hWnd );
	SetFocus( sww_state.hWnd );

}

/*
** SWimp_Init
**
** This routine is responsible for initializing the implementation
** specific stuff in a software rendering subsystem.
*/
int SWimp_Init( void *hInstance, void *wndProc )
{
	sww_state.hInstance = ( HINSTANCE ) hInstance;
	sww_state.wndproc = wndProc;

	return true;
}

/*
** SWimp_InitGraphics
**
** This initializes the software refresh's implementation specific
** graphics subsystem.  In the case of Windows it creates DIB or
** DDRAW surfaces.
**
** The necessary width and height parameters are grabbed from
** vid.width and vid.height.
*/
static qboolean SWimp_InitGraphics( qboolean fullscreen )
{
	// free resources in use
	SWimp_Shutdown ();
	vid.buffer = NULL;

	// create a new window
	VID_CreateWindow (sw_output_width, sw_output_height, WINDOW_STYLE);

	// initialize the appropriate subsystem
	if ( !fullscreen )
	{
		if ( !DIB_Init( &vid.buffer, &vid.rowbytes ) )
		{
			vid.buffer = 0;
			vid.rowbytes = 0;

			return false;
		}
	}
	else
	{
		if ( !DDRAW_Init( &vid.buffer, &vid.rowbytes, sw_output_width, sw_output_height ) )
		{
			vid.buffer = 0;
			vid.rowbytes = 0;

			return false;
		}
	}

	return true;
}

/* Menus use a native 8-bit target only while visible. The game frame
   stays on the normal scaled path during play. */
void SWimp_MenuBegin(void)
{
	DDSURFACEDESC desc;
	HRESULT result;
	int x,y,pitch,sx,stepx,stepy;
	const byte *src;
	byte *dest,*row;

	if(sw_menu_drawing || sw_menu_ready || !vid.buffer || sw_lost_buffer ||
	   (vid.width==sw_output_width && vid.height==sw_output_height))
		return;
	pitch=(sw_output_width+3)&~3;
	if(sw_state.fullscreen)
	{
		memset(&desc,0,sizeof(desc));
		desc.dwSize=sizeof(desc);
		if(!sw_menu_surface)
		{
			desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT;
			desc.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;
			desc.dwWidth=sw_output_width;
			desc.dwHeight=sw_output_height;
			result=sww_state.lpDirectDraw->lpVtbl->CreateSurface(sww_state.lpDirectDraw,
				&desc,&sw_menu_surface,NULL);
			if(result!=DD_OK)
			{
				SWimp_MenuError(result);
				return;
			}
		}
		memset(&desc,0,sizeof(desc));
		desc.dwSize=sizeof(desc);
		result=sw_menu_surface->lpVtbl->Lock(sw_menu_surface,NULL,&desc,DDLOCK_WAIT,NULL);
		if(result==DDERR_SURFACELOST && sw_menu_surface->lpVtbl->Restore(sw_menu_surface)==DD_OK)
			result=sw_menu_surface->lpVtbl->Lock(sw_menu_surface,NULL,&desc,DDLOCK_WAIT,NULL);
		if(result!=DD_OK)
		{
			SWimp_MenuError(result);
			return;
		}
		dest=desc.lpSurface;
		pitch=desc.lPitch;
	}
	else
	{
		if(!sw_menu_dib)
			sw_menu_dib=malloc(pitch*sw_output_height);
		if(!sw_menu_dib)
		{
			SWimp_MenuFallback();
			return;
		}
		/* Match the bottom-up DIB layout used by the Win95/98 scene buffer. */
		dest=sw_menu_dib+(sw_output_height-1)*pitch;
		pitch=-pitch;
	}
	stepx=(vid.width<<16)/sw_output_width;
	stepy=(vid.height<<16)/sw_output_height;
	for(y=0;y<sw_output_height;y++)
	{
		src=vid.buffer+((y*stepy)>>16)*vid.rowbytes;
		row=dest+y*pitch;
		for(x=0,sx=0;x<sw_output_width;x++,sx+=stepx)
			row[x]=src[sx>>16];
	}
	sw_menu_scene.width=vid.width;
	sw_menu_scene.height=vid.height;
	sw_menu_scene.pitch=vid.rowbytes;
	sw_menu_scene.buffer=vid.buffer;
	vid.width=sw_output_width;
	vid.height=sw_output_height;
	vid.rowbytes=pitch;
	vid.buffer=dest;
	sw_menu_drawing=true;
}

void SWimp_MenuEnd(void)
{
	HRESULT result=DD_OK;
	if(!sw_menu_drawing) return;
	if(sw_menu_surface)
		result=sw_menu_surface->lpVtbl->Unlock(sw_menu_surface,vid.buffer);
	vid.width=sw_menu_scene.width;
	vid.height=sw_menu_scene.height;
	vid.rowbytes=sw_menu_scene.pitch;
	vid.buffer=sw_menu_scene.buffer;
	sw_menu_drawing=false;
	sw_menu_ready=result==DD_OK;
	SWimp_MenuError(result);
}

static qboolean SWimp_PresentFullscreenMenu(void)
{
	RECT bounds={0,0,0,0};
	LPDIRECTDRAWSURFACE front=sww_state.lpddsFrontBuffer;
	LPDIRECTDRAWSURFACE target=sww_state.modex?sww_state.lpddsBackBuffer:front;
	HRESULT result;

	sw_menu_ready=false;
	bounds.right=sw_output_width;
	bounds.bottom=sw_output_height;
	if(sww_state.modex)
	{
		result=target->lpVtbl->BltFast(target,0,0,sw_menu_surface,&bounds,DDBLTFAST_WAIT);
		if(result==DD_OK)
			result=front->lpVtbl->Flip(front,NULL,DDFLIP_WAIT);
	}
	else
	{
		/* BltFast may reject a system-memory source on legacy drivers. */
		result=target->lpVtbl->Blt(target,&bounds,sw_menu_surface,&bounds,DDBLT_WAIT,NULL);
	}
	if(result==DDERR_SURFACELOST)
		front->lpVtbl->Restore(front);
	SWimp_MenuError(result);
	return result==DD_OK;
}

static qboolean SWimp_PresentWindowMenu(void)
{
	struct { BITMAPINFOHEADER header; RGBQUAD colors[256]; } info;
	int i;

	if(!sw_menu_ready) return false;
	sw_menu_ready=false;
	memset(&info,0,sizeof(info));
	info.header.biSize=sizeof(info.header);
	info.header.biWidth=sw_output_width;
	info.header.biHeight=sw_output_height;
	info.header.biPlanes=1;
	info.header.biBitCount=8;
	info.header.biClrUsed=256;
	for(i=0;i<256;i++)
	{
		info.colors[i].rgbRed=sw_state.currentpalette[i*4];
		info.colors[i].rgbGreen=sw_state.currentpalette[i*4+1];
		info.colors[i].rgbBlue=sw_state.currentpalette[i*4+2];
	}
	if(SetDIBitsToDevice(sww_state.hDC,0,0,sw_output_width,sw_output_height,
		0,0,0,sw_output_height,sw_menu_dib,(BITMAPINFO *)&info,DIB_RGB_COLORS)>0)
		return true;
	SWimp_MenuFallback();
	return false;
}

static void SWimp_ReleaseMenu(void)
{
	SWimp_MenuEnd();
	if(sw_menu_surface)
	{
		sw_menu_surface->lpVtbl->Release(sw_menu_surface);
		sw_menu_surface=NULL;
	}
	free(sw_menu_dib);
	sw_menu_dib=NULL;
	sw_menu_ready=false;
}

/* Stretch directly to the primary surface, the same destination as the
   native linear path. Let DirectDraw choose the available blitter. */
static void SWimp_StretchFullscreen(RECT *source)
{
	LPDIRECTDRAWSURFACE front=sww_state.lpddsFrontBuffer;
	LPDIRECTDRAWSURFACE buffer=sww_state.lpddsOffScreenBuffer;
	RECT dest={0,0,0,0};
	HRESULT result;

	dest.right=sw_output_width;
	dest.bottom=sw_output_height;
	result=front->lpVtbl->Blt(front, &dest, buffer, source, DDBLT_WAIT, NULL);
	if (result==DDERR_SURFACELOST && front->lpVtbl->Restore(front)==DD_OK)
		result=front->lpVtbl->Blt(front, &dest, buffer, source, DDBLT_WAIT, NULL);

	switch (result)
	{
	case DD_OK:
	case DDERR_SURFACELOST:
	case DDERR_NOEXCLUSIVEMODE:
	case DDERR_WRONGMODE:
	case DDERR_SURFACEBUSY:
	case DDERR_WASSTILLDRAWING:
		return;
	default:
		ri.Con_Printf(PRINT_ALL, "DirectDraw upscale failed (0x%08lx). Restoring native fullscreen render size.\n",
			(unsigned long)result);
		ri.Cvar_SetValue("sw_render_scale", 100);
	}
}

/*
** SWimp_EndFrame
**
** Present the current software frame through GDI or DirectDraw.
*/
/* BDGN 3.56c: for sw_paths - the frame is stretched to the screen (the test SWimp_EndFrame uses) */
qboolean SWimp_Scaled (void)
{
	return vid.width != sw_output_width || vid.height != sw_output_height;
}

void SWimp_EndFrame (void)
{
	if (sw_lost_buffer)
		return;
	if ( !sw_state.fullscreen )
	{
		if (SWimp_PresentWindowMenu())
			return;
		if ( sww_state.palettized )
		{
//			holdpal = SelectPalette(hdcScreen, hpalDIB, FALSE);
//			RealizePalette(hdcScreen);
		}

		if (vid.width != sw_output_width || vid.height != sw_output_height)
		{
			/* BDGN 3.58b: sw_upscale 0 = Windows StretchBlt; 1 (default) = our own widening for a factor that is
			   not a whole number, StretchBlt for 2x, 3x...; 2 = always our own */
			static cvar_t *sw_upscale;
			int mode;
			qboolean whole;
			if (!sw_upscale)
				sw_upscale = ri.Cvar_Get("sw_upscale", "1", CVAR_ARCHIVE);
			mode = (int)sw_upscale->value;
			whole = sw_output_width % vid.width == 0 && sw_output_height % vid.height == 0;
			if ((mode == 2 || (mode == 1 && !whole)) && DIB_UpscalePresent(sw_output_width, sw_output_height))
				return;
			if (StretchBlt(sww_state.hDC, 0, 0, sw_output_width, sw_output_height,
				sww_state.hdcDIBSection, 0, 0, vid.width, vid.height, SRCCOPY))
				return;
			ri.Con_Printf(PRINT_ALL, "Software upscale unavailable. Restoring native render size.\n");
			ri.Cvar_SetValue("sw_render_scale", 100);
		}

		BitBlt( sww_state.hDC,
			    0, 0,
				vid.width,
				vid.height,
				sww_state.hdcDIBSection,
				0, 0,
				SRCCOPY );

		if ( sww_state.palettized )
		{
//			SelectPalette(hdcScreen, holdpal, FALSE);
		}
	}
	else
	{
		RECT r;
		HRESULT rval;
		DDSURFACEDESC ddsd;
		qboolean scaled = vid.width != sw_output_width || vid.height != sw_output_height;
		LPDIRECTDRAWSURFACE buffer = sww_state.lpddsOffScreenBuffer;

		r.left = 0;
		r.top = 0;
		r.right = vid.width;
		r.bottom = vid.height;

		rval = buffer->lpVtbl->Unlock(buffer, vid.buffer);

		if (scaled)
		{
			if (rval == DD_OK && (!sw_menu_ready || !SWimp_PresentFullscreenMenu()))
				SWimp_StretchFullscreen(&r);
			sw_menu_ready=false;
		}
		else if ( sww_state.modex )
		{
			if ( ( rval = sww_state.lpddsBackBuffer->lpVtbl->BltFast( sww_state.lpddsBackBuffer,
																	0, 0,
																	sww_state.lpddsOffScreenBuffer, 
																	&r, 
																	DDBLTFAST_WAIT ) ) == DDERR_SURFACELOST )
			{
				sww_state.lpddsBackBuffer->lpVtbl->Restore( sww_state.lpddsBackBuffer );
				sww_state.lpddsBackBuffer->lpVtbl->BltFast( sww_state.lpddsBackBuffer,
															0, 0,
															sww_state.lpddsOffScreenBuffer, 
															&r, 
															DDBLTFAST_WAIT );
			}

			/* BDGN 3.58m: no wait for the vertical blank while a timedemo (every benchmark) runs; DirectX before 7 does
			   not know DDFLIP_NOVSYNC and refuses it, then the plain flip is used */
			{
			static cvar_t *td;
			static int novsync_refused;
			DWORD flipflags = DDFLIP_WAIT;
			if ( !td ) td = ri.Cvar_Get( "timedemo", "0", 0 );
			if ( !novsync_refused ) flipflags |= DDFLIP_NOVSYNC;	/* 3.58o: always, not only in timedemos */
			(void)td;
			rval = sww_state.lpddsFrontBuffer->lpVtbl->Flip( sww_state.lpddsFrontBuffer, NULL, flipflags );
			if ( rval == DDERR_INVALIDPARAMS && ( flipflags & DDFLIP_NOVSYNC ) )
			{
				novsync_refused = 1;
				rval = sww_state.lpddsFrontBuffer->lpVtbl->Flip( sww_state.lpddsFrontBuffer, NULL, DDFLIP_WAIT );
			}
			if ( rval == DDERR_SURFACELOST )
			{
				sww_state.lpddsFrontBuffer->lpVtbl->Restore( sww_state.lpddsFrontBuffer );
				sww_state.lpddsFrontBuffer->lpVtbl->Flip( sww_state.lpddsFrontBuffer, NULL, DDFLIP_WAIT );
			}
			}
		}
		else
		{
			if ( ( rval = sww_state.lpddsBackBuffer->lpVtbl->BltFast( sww_state.lpddsFrontBuffer,
																	0, 0,
																	sww_state.lpddsOffScreenBuffer, 
																	&r, 
																	DDBLTFAST_WAIT ) ) == DDERR_SURFACELOST )
			{
				sww_state.lpddsBackBuffer->lpVtbl->Restore( sww_state.lpddsFrontBuffer );
				sww_state.lpddsBackBuffer->lpVtbl->BltFast( sww_state.lpddsFrontBuffer,
															0, 0,
															sww_state.lpddsOffScreenBuffer, 
															&r, 
															DDBLTFAST_WAIT );
			}
		}

		memset( &ddsd, 0, sizeof( ddsd ) );
		ddsd.dwSize = sizeof( ddsd );
	
		rval = buffer->lpVtbl->Lock(buffer, NULL, &ddsd, DDLOCK_WAIT, NULL);
		if (scaled && rval == DDERR_SURFACELOST && buffer->lpVtbl->Restore(buffer) == DD_OK)
			rval = buffer->lpVtbl->Lock(buffer, NULL, &ddsd, DDLOCK_WAIT, NULL);
		if (scaled && rval != DD_OK)
		{
			/* The next BeginFrame recreates the surfaces. Keep a valid buffer
			   for palette changes and console commands before then. */
			sw_lost_buffer = calloc(vid.height, vid.width);
			if (!sw_lost_buffer)
				ri.Sys_Error(ERR_FATAL, "Couldn't allocate Software recovery buffer");
			vid.buffer = sw_lost_buffer;
			vid.rowbytes = vid.width;
			sw_mode->modified = true;
			return;
		}

		vid.buffer = ddsd.lpSurface;
		vid.rowbytes = ddsd.lPitch;
	}
}

/*
** SWimp_SetMode
*/
static qboolean SWimp_RenderSize(int width, int height, float scale, int *w, int *h)
{
	int scaled_width, scaled_height, percent=RenderScalePercent(RenderScaleStep(scale));

	*w=width;
	*h=height;
	if (percent == 100)
		return false;
	scaled_width=RenderScaleWidth(width, percent);
	scaled_height=RenderScaleHeight(height, percent);
	/* BDGN 3.57c: half rows / half or quarter columns no longer shrink the whole buffer (the HUD and menus came out
	   coarse); R_RenderFrame draws only the 3D view smaller and widens it in place */
	if (scaled_width<160 || scaled_height<120 || scaled_width>MAXWIDTH || scaled_height>MAXHEIGHT)
		return false;
	*w=scaled_width;
	*h=scaled_height;
	return true;
}

static qboolean SWimp_InitNativeGraphics(qboolean fullscreen, int *width, int *height)
{
	ri.Con_Printf(PRINT_ALL, "Software upscale unavailable. Using native render size.\n");
	*width=sw_output_width;
	*height=sw_output_height;
	ri.Cvar_SetValue("sw_render_scale", 100);
	sw_render_scale->modified=false;
	return SWimp_InitGraphics(fullscreen);
}

rserr_t SWimp_SetMode( int *pwidth, int *pheight, int mode, qboolean fullscreen )
{
	const char *win_fs[] = { "W", "FS" };
	rserr_t retval = rserr_ok;
	qboolean scaled;

	ri.Con_Printf (PRINT_ALL, "setting mode %d:", mode );

	if ( !ri.Vid_GetModeInfo( pwidth, pheight, mode ) )
	{
		ri.Con_Printf( PRINT_ALL, " invalid mode\n" );
		return rserr_invalid_mode;
	}

	ri.Con_Printf( PRINT_ALL, " %d %d %s\n", *pwidth, *pheight, win_fs[fullscreen] );
    if (*pwidth <= 0 || *pheight <= 0 || *pwidth > MAXWIDTH || *pheight > MAXHEIGHT)
        return rserr_invalid_mode;


	sw_output_width=*pwidth;
	sw_output_height=*pheight;
	scaled=SWimp_RenderSize(sw_output_width, sw_output_height,
		sw_render_scale->value, pwidth, pheight);

	sww_state.initializing = true;
	if ( fullscreen )
	{
		if (!SWimp_InitGraphics(true) && !(scaled && SWimp_InitNativeGraphics(true, pwidth, pheight)))
		{
			if ( SWimp_InitGraphics( 0 ) )
			{
				// mode is legal but not as fullscreen
				fullscreen = 0;
				retval = rserr_invalid_fullscreen;
			}
			else
			{
				// failed to set a valid mode in windowed mode
				retval = rserr_unknown;
			}
		}
	}
	else
	{
		qboolean ok=SWimp_InitGraphics(false);
		if (scaled && (!ok || !(GetDeviceCaps(sww_state.hDC, RASTERCAPS)&RC_STRETCHBLT)
			|| !SetStretchBltMode(sww_state.hDC, COLORONCOLOR)))
			ok=SWimp_InitNativeGraphics(false, pwidth, pheight);
		if (!ok)
			return rserr_unknown;
	}

	sw_state.fullscreen = fullscreen;
	if (retval != rserr_unknown)
	{
		ri.Cvar_SetValue("sw_output_width", sw_output_width);
		ri.Cvar_SetValue("sw_output_height", sw_output_height);
		ri.Vid_NewWindow(*pwidth, *pheight);
		ri.Con_Printf(PRINT_ALL, "Software render %dx%d, output %dx%d\n",
			*pwidth, *pheight, sw_output_width, sw_output_height);
	}
#if 0
	if ( retval != rserr_unknown )
	{
		if ( retval == rserr_invalid_fullscreen ||
			 ( retval == rserr_ok && !fullscreen ) )
		{
			SetWindowLong( sww_state.hWnd, GWL_STYLE, WINDOW_STYLE );
		}
	}
#endif
	R_GammaCorrectAndSetPalette( ( const unsigned char * ) d_8to24table );
	sww_state.initializing = true;

	return retval;
}

/*
** SWimp_SetPalette
**
** System specific palette setting routine.  A NULL palette means
** to use the existing palette.  The palette is expected to be in
** a padded 4-byte xRGB format.
*/
void SWimp_SetPalette( const unsigned char *palette )
{
	// MGL - what the fuck was kendall doing here?!
	// clear screen to black and change palette
	//	for (i=0 ; i<vid.height ; i++)
	//		memset (vid.buffer + i*vid.rowbytes, 0, vid.width);

	if ( !palette )
		palette = ( const unsigned char * ) sw_state.currentpalette;

	if ( !sw_state.fullscreen )
	{
		DIB_SetPalette( ( const unsigned char * ) palette );
	}
	else
	{
		DDRAW_SetPalette( ( const unsigned char * ) palette );
	}
}

/*
** SWimp_Shutdown
**
** System specific graphics subsystem shutdown routine.  Destroys
** DIBs or DDRAW surfaces as appropriate.
*/
void SWimp_Shutdown( void )
{
	ri.Con_Printf( PRINT_ALL, "Shutting down SW imp\n" );
	SWimp_ReleaseMenu();
	DIB_Shutdown();
	if (sw_lost_buffer)
	{
		free(sw_lost_buffer);
		sw_lost_buffer = NULL;
		vid.buffer = NULL;
	}
	DDRAW_Shutdown();

	if ( sww_state.hWnd )
	{
		ri.Con_Printf( PRINT_ALL, "...destroying window\n" );
		ShowWindow( sww_state.hWnd, SW_SHOWNORMAL );	// prevents leaving empty slots in the taskbar
		DestroyWindow (sww_state.hWnd);
		sww_state.hWnd = NULL;
		UnregisterClass (WINDOW_CLASS_NAME, sww_state.hInstance);
	}
}

/*
** SWimp_AppActivate
*/
void SWimp_AppActivate( qboolean active )
{
	if ( active )
	{
		if ( sww_state.hWnd )
		{
			SetForegroundWindow( sww_state.hWnd );
			ShowWindow( sww_state.hWnd, SW_RESTORE );
		}
	}
	else
	{
		if ( sww_state.hWnd )
		{
			if ( sww_state.initializing )
				return;
			if ( vid_fullscreen->value )
				ShowWindow( sww_state.hWnd, SW_MINIMIZE );
		}
	}
}

//===============================================================================


/*
================
Sys_MakeCodeWriteable
================
*/
void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{
	DWORD  flOldProtect;

	if (!VirtualProtect((LPVOID)startaddr, length, PAGE_READWRITE, &flOldProtect))
 		ri.Sys_Error(ERR_FATAL, "Protection change failed\n");
}

/*
** Sys_SetFPCW
**
** For reference:
**
** 1
** 5               0
** xxxxRRPP.xxxxxxxx
**
** PP = 00 = 24-bit single precision
** PP = 01 = reserved
** PP = 10 = 53-bit double precision
** PP = 11 = 64-bit extended precision
**
** RR = 00 = round to nearest
** RR = 01 = round down (towards -inf, floor)
** RR = 10 = round up (towards +inf, ceil)
** RR = 11 = round to zero (truncate/towards 0)
**
*/
#if !id386
void Sys_SetFPCW (void)
{
}
#else
unsigned fpu_ceil_cw, fpu_chop_cw, fpu_full_cw, fpu_cw, fpu_pushed_cw;
unsigned fpu_sp24_cw, fpu_sp24_ceil_cw;

void Sys_SetFPCW( void )
{
	__asm xor eax, eax

	__asm fnstcw  word ptr fpu_cw
	__asm mov ax, word ptr fpu_cw

	__asm and ah, 0f0h
	__asm or  ah, 003h          ; round to nearest mode, extended precision
	__asm mov fpu_full_cw, eax

	__asm and ah, 0f0h
	__asm or  ah, 00fh          ; RTZ/truncate/chop mode, extended precision
	__asm mov fpu_chop_cw, eax

	__asm and ah, 0f0h
	__asm or  ah, 00bh          ; ceil mode, extended precision
	__asm mov fpu_ceil_cw, eax

	__asm and ah, 0f0h          ; round to nearest, 24-bit single precision
	__asm mov fpu_sp24_cw, eax

	__asm and ah, 0f0h          ; ceil mode, 24-bit single precision
	__asm or  ah, 008h          ; 
	__asm mov fpu_sp24_ceil_cw, eax
}
#endif

