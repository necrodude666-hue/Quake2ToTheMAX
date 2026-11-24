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
// Main windowed and fullscreen graphics interface module. This module
// is used for both the software and OpenGL rendering versions of the
// Quake refresh engine.
#include <assert.h>
#include <float.h>
#include <ctype.h>	/* BDGN 3.52k: tolower in VID_DescId */

#include "../client/client.h"
#include "../client/vid_scale.h"
#include "winquake.h"
#include "../qcommon/q3e_3dnow.h"	/* BDGN 3.52l: MMX / 3DNow! / SSE for the driver options */
static void VID_Hardware_f (void);	/* BDGN 3.52o: used in VID_Init, defined further down */
//#include "zmouse.h"

// Structure containing functions exported from refresh DLL
refexport_t	re;

cvar_t *win_noalttab;

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL (WM_MOUSELAST+1)  // message that will be supported by the OS 
#endif

static UINT MSH_MOUSEWHEEL;

// Console variables that we need to access from this module
cvar_t		*vid_gamma;
cvar_t		*vid_ref;			// Name of Refresh DLL loaded
cvar_t		*vid_xpos;			// X coordinate of window position
cvar_t		*vid_ypos;			// Y coordinate of window position
cvar_t		*vid_fullscreen;

// Global variables used internally by this module
viddef_t	viddef;				// global video state; used by other modules (BDGN 3.50: the virtual 2D screen)
int			vid_scale2d = 1;	// BDGN 3.50: real screen = viddef * vid_scale2d (rounded down)
static int	vid_real_width, vid_real_height;
static int	vid_output_width, vid_output_height;
static cvar_t	*scr_scale;		// 0 = auto (height / 600, at least 640x480), else the wanted scale
HINSTANCE	reflib_library;		// Handle to refresh DLL 
qboolean	reflib_active = 0;
static void (*native_menu_begin)(void),(*native_menu_end)(void);

void VID_BeginNativeMenu(void)
{
	if(native_menu_begin && native_menu_end) native_menu_begin();
}

void VID_EndNativeMenu(void)
{
	if(native_menu_end) native_menu_end();
}

HWND        cl_hwnd;            // Main window handle for life of program

#define VID_NUM_MODES ( sizeof( vid_modes ) / sizeof( vid_modes[0] ) )

LONG WINAPI MainWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

static qboolean s_alttab_disabled;

extern	unsigned	sys_msg_time;

/*
** WIN32 helper functions
*/
extern qboolean s_win95;

static void WIN_DisableAltTab( void )
{
	if ( s_alttab_disabled )
		return;

	if ( s_win95 )
	{
		BOOL old;

		SystemParametersInfo( SPI_SCREENSAVERRUNNING, 1, &old, 0 );
	}
	else
	{
		RegisterHotKey( 0, 0, MOD_ALT, VK_TAB );
		RegisterHotKey( 0, 1, MOD_ALT, VK_RETURN );
	}
	s_alttab_disabled = true;
}

static void WIN_EnableAltTab( void )
{
	if ( s_alttab_disabled )
	{
		if ( s_win95 )
		{
			BOOL old;

			SystemParametersInfo( SPI_SCREENSAVERRUNNING, 0, &old, 0 );
		}
		else
		{
			UnregisterHotKey( 0, 0 );
			UnregisterHotKey( 0, 1 );
		}

		s_alttab_disabled = false;
	}
}

/*
==========================================================================

DLL GLUE

==========================================================================
*/

#define	MAXPRINTMSG	4096
void VID_Printf (int print_level, char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];
	static qboolean	inupdate;
	
	va_start (argptr,fmt);
	vsprintf (msg,fmt,argptr);
	va_end (argptr);

	if (print_level == PRINT_ALL)
	{
		Com_Printf ("%s", msg);
	}
	else if ( print_level == PRINT_DEVELOPER )
	{
		Com_DPrintf ("%s", msg);
	}
	else if ( print_level == PRINT_ALERT )
	{
		MessageBox( 0, msg, "PRINT_ALERT", MB_ICONWARNING );
		OutputDebugString( msg );
	}
}

void VID_Error (int err_level, char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];
	static qboolean	inupdate;
	
	va_start (argptr,fmt);
	vsprintf (msg,fmt,argptr);
	va_end (argptr);

	Com_Error (err_level,"%s", msg);
}

//==========================================================================

byte        scantokey[128] = 
					{ 
//  0           1       2       3       4       5       6       7 
//  8           9       A       B       C       D       E       F 
	0  ,    27,     '1',    '2',    '3',    '4',    '5',    '6', 
	'7',    '8',    '9',    '0',    '-',    '=',    K_BACKSPACE, 9, // 0 
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i', 
	'o',    'p',    '[',    ']',    13 ,    K_CTRL,'a',  's',      // 1 
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';', 
	'\'' ,    '`',    K_SHIFT,'\\',  'z',    'x',    'c',    'v',      // 2 
	'b',    'n',    'm',    ',',    '.',    '/',    K_SHIFT,'*', 
	K_ALT,' ',   0  ,    K_F1, K_F2, K_F3, K_F4, K_F5,   // 3 
	K_F6, K_F7, K_F8, K_F9, K_F10,  K_PAUSE,    0  , K_HOME, 
	K_UPARROW,K_PGUP,K_KP_MINUS,K_LEFTARROW,K_KP_5,K_RIGHTARROW, K_KP_PLUS,K_END, //4 
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,0,0,             0,              K_F11, 
	K_F12,0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,        // 5
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,        // 6 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0         // 7 
}; 

/*
=======
MapKey

Map from windows to quake keynums
=======
*/
int MapKey (int key)
{
	int result;
	int modified = ( key >> 16 ) & 255;
	qboolean is_extended = false;

	if ( modified > 127)
		return 0;

	if ( key & ( 1 << 24 ) )
		is_extended = true;

	result = scantokey[modified];

	if ( !is_extended )
	{
		switch ( result )
		{
		case K_HOME:
			return K_KP_HOME;
		case K_UPARROW:
			return K_KP_UPARROW;
		case K_PGUP:
			return K_KP_PGUP;
		case K_LEFTARROW:
			return K_KP_LEFTARROW;
		case K_RIGHTARROW:
			return K_KP_RIGHTARROW;
		case K_END:
			return K_KP_END;
		case K_DOWNARROW:
			return K_KP_DOWNARROW;
		case K_PGDN:
			return K_KP_PGDN;
		case K_INS:
			return K_KP_INS;
		case K_DEL:
			return K_KP_DEL;
		default:
			return result;
		}
	}
	else
	{
		switch ( result )
		{
		case 0x0D:
			return K_KP_ENTER;
		case 0x2F:
			return K_KP_SLASH;
		case 0xAF:
			return K_KP_PLUS;
		}
		return result;
	}
}

void AppActivate(BOOL fActive, BOOL minimize)
{
	Minimized = minimize;

	Key_ClearStates();

	// we don't want to act like we're active if we're minimized
	if (fActive && !Minimized)
		ActiveApp = true;
	else
		ActiveApp = false;

	// minimize/restore mouse-capture on demand
	if (!ActiveApp)
	{
		IN_Activate (false);
		CDAudio_Activate (false);
		S_Activate (false);

		if ( win_noalttab->value )
		{
			WIN_EnableAltTab();
		}
	}
	else
	{
		IN_Activate (true);
		CDAudio_Activate (true);
		S_Activate (true);
		if ( win_noalttab->value )
		{
			WIN_DisableAltTab();
		}
	}
}

/*
====================
MainWndProc

main window procedure
====================
*/
LONG WINAPI MainWndProc (
    HWND    hWnd,
    UINT    uMsg,
    WPARAM  wParam,
    LPARAM  lParam)
{
	LONG			lRet = 0;

	if ( uMsg == MSH_MOUSEWHEEL )
	{
		if ( ( ( int ) wParam ) > 0 )
		{
			Key_Event( K_MWHEELUP, true, sys_msg_time );
			Key_Event( K_MWHEELUP, false, sys_msg_time );
		}
		else
		{
			Key_Event( K_MWHEELDOWN, true, sys_msg_time );
			Key_Event( K_MWHEELDOWN, false, sys_msg_time );
		}
        return DefWindowProc (hWnd, uMsg, wParam, lParam);
	}

	switch (uMsg)
	{
	case WM_MOUSEWHEEL:
		/*
		** this chunk of code theoretically only works under NT4 and Win98
		** since this message doesn't exist under Win95
		*/
		if ( ( short ) HIWORD( wParam ) > 0 )
		{
			Key_Event( K_MWHEELUP, true, sys_msg_time );
			Key_Event( K_MWHEELUP, false, sys_msg_time );
		}
		else
		{
			Key_Event( K_MWHEELDOWN, true, sys_msg_time );
			Key_Event( K_MWHEELDOWN, false, sys_msg_time );
		}
		break;

	case WM_HOTKEY:
		return 0;

	case WM_CREATE:
		cl_hwnd = hWnd;

		MSH_MOUSEWHEEL = RegisterWindowMessage("MSWHEEL_ROLLMSG"); 
        return DefWindowProc (hWnd, uMsg, wParam, lParam);

	case BDGN_WM_INPUT:
		IN_RawMouseInput(lParam);
		return DefWindowProc(hWnd,uMsg,wParam,lParam);

	case WM_SIZE:
		if (ActiveApp && hWnd==cl_hwnd)
			IN_Activate(true);
		return DefWindowProc(hWnd,uMsg,wParam,lParam);

	case WM_PAINT:
		SCR_DirtyScreen ();	// force entire screen to update next frame
        return DefWindowProc (hWnd, uMsg, wParam, lParam);

	case WM_CLOSE:
		/* BDGN 3.57h: Alt+F4 or the window's close button quit the game the normal way, so config.cfg is written
		   and the next start uses the settings of this one (DefWindowProc destroyed the window without it) */
		Cbuf_AddText ("quit\n");
		return 0;

	case WM_ENDSESSION:
		if (wParam)
			CL_WriteConfiguration ();	/* BDGN 3.57h: Windows logs off or shuts down while the game runs */
		return 0;

	case WM_DESTROY:
		// let sound and input know about this?
		cl_hwnd = NULL;
        return DefWindowProc (hWnd, uMsg, wParam, lParam);

	case WM_ACTIVATE:
		{
			int	fActive, fMinimized;

			// KJB: Watch this for problems in fullscreen modes with Alt-tabbing.
			fActive = LOWORD(wParam);
			fMinimized = (BOOL) HIWORD(wParam);

			AppActivate( fActive != WA_INACTIVE, fMinimized);

			if ( reflib_active )
				re.AppActivate( !( fActive == WA_INACTIVE ) );
		}
        return DefWindowProc (hWnd, uMsg, wParam, lParam);

	case WM_MOVE:
		{
			int		xPos, yPos;
			RECT r;
			int		style;

			if (!vid_fullscreen->value)
			{
				xPos = (short) LOWORD(lParam);    // horizontal position 
				yPos = (short) HIWORD(lParam);    // vertical position 

				r.left   = 0;
				r.top    = 0;
				r.right  = 1;
				r.bottom = 1;

				style = GetWindowLong( hWnd, GWL_STYLE );
				AdjustWindowRect( &r, style, FALSE );

				Cvar_SetValue( "vid_xpos", xPos + r.left);
				Cvar_SetValue( "vid_ypos", yPos + r.top);
				vid_xpos->modified = false;
				vid_ypos->modified = false;
				if (ActiveApp)
					IN_Activate (true);
			}
		}
        return DefWindowProc (hWnd, uMsg, wParam, lParam);

// this is complicated because Win32 seems to pack multiple mouse events into
// one update sometimes, so we always check all states and look for events
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
		{
			int	temp;

			temp = 0;

			if (wParam & MK_LBUTTON)
				temp |= 1;

			if (wParam & MK_RBUTTON)
				temp |= 2;

			if (wParam & MK_MBUTTON)
				temp |= 4;

			IN_MouseEvent (temp);
		}
		break;

	case WM_SYSCOMMAND:
		if ( wParam == SC_SCREENSAVE )
			return 0;
        return DefWindowProc (hWnd, uMsg, wParam, lParam);
	case WM_SYSKEYDOWN:
		if ( wParam == 13 )
		{
			if ( vid_fullscreen )
			{
				Cvar_SetValue( "vid_fullscreen", !vid_fullscreen->value );
			}
			return 0;
		}
		// fall through
	case WM_KEYDOWN:
		Key_Event( MapKey( lParam ), true, sys_msg_time);
		break;

	case WM_SYSKEYUP:
	case WM_KEYUP:
		Key_Event( MapKey( lParam ), false, sys_msg_time);
		break;

	case MM_MCINOTIFY:
		{
			LONG CDAudio_MessageHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
			lRet = CDAudio_MessageHandler (hWnd, uMsg, wParam, lParam);
		}
		break;

	default:	// pass all unhandled messages to DefWindowProc
        return DefWindowProc (hWnd, uMsg, wParam, lParam);
    }

    /* return 0 if handled message, 1 if not */
    return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

/*
============
VID_Restart_f

Console command to re-start the video mode and refresh DLL. We do this
simply by setting the modified flag for the vid_ref variable, which will
cause the entire video mode and refresh DLL to be reset on the next frame.
============
*/
void VID_DefaultConfig_f (void);
qboolean VID_ExecPreset (const char *file);

void VID_Restart_f (void)
{
	vid_ref->modified = true;
}

void VID_Front_f( void )
{
	SetWindowLong( cl_hwnd, GWL_EXSTYLE, WS_EX_TOPMOST );
	SetForegroundWindow( cl_hwnd );
}

/*
** VID_GetModeInfo
*/
typedef struct vidmode_s
{
	const char *description;
	int         width, height;
	int         mode;
} vidmode_t;

vidmode_t vid_modes[] =
{
	//4:3 and 5:4
	{ "Mode 0: 320x240",    320, 240,    0 },
	{ "Mode 1: 400x300",    400, 300,    1 },
	{ "Mode 2: 512x384",    512, 384,    2 },
	{ "Mode 3: 640x480",    640, 480,    3 },
	{ "Mode 4: 800x600",    800, 600,    4 },
	{ "Mode 5: 960x720",    960, 720,    5 },
	{ "Mode 6: 1024x768",   1024, 768,   6 },
	{ "Mode 7: 1152x864",   1152, 864,   7 },
	{ "Mode 8: 1280x960",   1280, 960,   8 },
	{ "Mode 9: 1280x1024",  1280, 1024,  9 },	// I NEED THIS!
	{ "Mode 10: 1600x1200", 1600, 1200, 10 },
	{ "Mode 11: 2048x1536", 2048, 1536, 11 },
	//testing
	//16:9 and 16:10
	{ "Mode 12: 1280x720", 1280, 720,   12 },
	{ "Mode 13: 1280x800", 1280, 800,   13 },
	{ "Mode 14: 1366x768", 1366, 768,   14 },
	{ "Mode 15: 1440x900", 1440, 900,   15 },
	{ "Mode 16: 1600x900", 1600, 900,   16 },
	{ "Mode 17: 1680x1050", 1680, 1050, 17 },
	{ "Mode 18: 1920x1080", 1920, 1080, 18 },
	{ "Mode 19: 1920x1200", 1920, 1200, 19 },
	{ "Mode 20: 2560x1440", 2560, 1440, 20 },
	{ "Mode 21: 2560x1600", 2560, 1600, 21 },
	{ "Mode 22: 3200x1800", 3200, 1800, 22 },
	{ "Mode 23: 3840x2160", 3840, 2160, 23 },
	{ "Mode 24: 3840x2400", 3840, 2400, 24 },
	//21:9
	{ "Mode 25: 2560x1080", 2560, 1080, 25 },
	{ "Mode 26: 3440x1440", 3440, 1440, 26 },
	{ "Mode 27: 3840x1600", 3840, 1600, 27 },
	//
};

qboolean VID_GetModeInfo( int *width, int *height, int mode )
{
	if ( mode < 0 || mode >= VID_NUM_MODES )
		return false;

	*width  = vid_modes[mode].width;
	*height = vid_modes[mode].height;

	return true;
}

/*
** VID_UpdateWindowPosAndSize
*/
void VID_UpdateWindowPosAndSize( int x, int y )
{
	RECT r;
	int		style;
	int		w, h;

	r.left   = 0;
	r.top    = 0;
	r.right  = vid_output_width;
	r.bottom = vid_output_height;

	style = GetWindowLong( cl_hwnd, GWL_STYLE );
	AdjustWindowRect( &r, style, FALSE );

	w = r.right - r.left;
	h = r.bottom - r.top;

	MoveWindow( cl_hwnd, vid_xpos->value, vid_ypos->value, w, h, TRUE );
}

/*
** VID_NewWindow
*/
/*
** VID_ApplyScale (BDGN 3.50): picks the 2D scale for the real screen and shrinks viddef to match.
** Only this source's ref_gl ("gl") draws 2D scaled; any other refresh gets scale 1.
*/
static void VID_ApplyScale (void)
{
	int scale = 1;
	if (vid_real_width <= 0 || vid_real_height <= 0)
		return;
	vid_output_width = vid_real_width;
	vid_output_height = vid_real_height;
	if (vid_ref && !strcmp(vid_ref->string, "soft") && Cvar_VariableValue("sw_bdgn_features") >= 3526)
	{
		int width = (int)Cvar_VariableValue("sw_output_width");
		int height = (int)Cvar_VariableValue("sw_output_height");
		if (width > 0 && height > 0)
		{
			vid_output_width = width;
			vid_output_height = height;
		}
	}
	if (scr_scale && vid_ref && (!strcmp (vid_ref->string, "gl") ||
		(!strcmp (vid_ref->string, "glam") && Cvar_VariableValue("glam_bdgn_features") >= 3516) ||
        ((!strcmp (vid_ref->string, "soft") ||
          (!strcmp(vid_ref->string,"swam") && Cvar_VariableValue("swam_bdgn_features")>=3512)) &&
          Cvar_VariableValue("sw_bdgn_features") >= 3518)))
		scale = VID_Scale2D (vid_output_width, vid_output_height, (int)scr_scale->value);
	vid_scale2d = scale;
	viddef.width  = vid_output_width / scale;
	viddef.height = vid_output_height / scale;
	Cvar_SetValue ("scr_scale_active", (float)scale);	// read by ref_gl for its 2D projection
	cl.force_refdef = true;
}

void VID_GetRealSize (int *width, int *height)
{
	*width = vid_real_width > 0 ? vid_real_width : viddef.width;
	*height = vid_real_height > 0 ? vid_real_height : viddef.height;
}

void VID_RectToRealPixels (int *x, int *y, int *width, int *height)
{
	VID_RectToRender (x, y, width, height, vid_scale2d, (int)viddef.width, (int)viddef.height,
		vid_output_width, vid_output_height, vid_real_width, vid_real_height);
}

void VID_NewWindow ( int width, int height)
{
	vid_real_width = width;
	vid_real_height = height;
	viddef.width  = width;
	viddef.height = height;
	VID_ApplyScale ();

	cl.force_refdef = true;		// can't use a paused refdef
}

void VID_FreeReflib (void)
{
	if ( !FreeLibrary( reflib_library ) )
		Com_Error( ERR_FATAL, "Reflib FreeLibrary failed" );
	memset (&re, 0, sizeof(re));
	reflib_library = NULL;
	reflib_active  = false;
	native_menu_begin=native_menu_end=NULL;
}

/*
==============
VID_LoadRefresh
==============
*/
/*
BDGN 3.52l: 3dfx driver options. The 3dfx drivers read their switches from the environment (Glide 2/3 and 3dfx's
OpenGL DLLs: FX_GLIDE_*, SST_*, SSTV2_*, SSTH3_*, GL_FORCE_*) and WickedGL from HKEY_LOCAL_MACHINE\Software\Metabyte
(DWORD values WickedGL_3B, WickedGL_MipMap_Dither). They are set here, in quake2.exe's environment, before the
renderer loads the driver; a change in the video menu restarts the renderer. Cvars (saved):
  fx_triplebuffer 0/1    three colour buffers (FX_GLIDE_ALLOC_COLOR / SSTV2_ / SSTH3_ALLOC_COLOR 3, WickedGL_3B)
  fx_refresh 0/60/72/75/85/100/120   monitor refresh in Hz (SST_ / SSTV2_ / SSTH3_SCREENREFRESH); 0 = driver default
  fx_lod_dither 0/1      dither between mip levels (FX_GLIDE_LOD_DITHER, WickedGL_MipMap_Dither): smoother, slower
  fx_fastmem 0/1         Voodoo2 memory timing: fast RAS reads and PCI reads (SSTV2_FASTMEM_RAS_READS, SSTV2_FASTPCIRD) (test)
  fx_videofilter 0/1     Voodoo2 16-bit output filter (SSTV2_VIDEO_FILTER_DISABLE when 0)
  fx_cpupath 0/1         miniGL CPU code: 1 = 3DNow! or MMX path forced when the CPU has it (GL_FORCE_K3D / GL_FORCE_MMX)
The swap interval follows gl_swapinterval (FX_GLIDE_SWAPINTERVAL, SST_ / SSTV2_ / SSTH3_SWAPINTERVAL).
*/
static void VID_PutEnv (const char *name, const char *value)
{
	char	buf[128];
	Com_sprintf( buf, sizeof( buf ), "%s=%s", name, value ? value : "" );
	_putenv( buf );		/* "NAME=" removes it */
}
static void VID_WickedOption (const char *name, int value)
{
	HKEY	k;
	DWORD	v = (DWORD)value;
	if ( RegCreateKeyEx( HKEY_LOCAL_MACHINE, "Software\\Metabyte", 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL ) != ERROR_SUCCESS )
		return;		/* no rights (NT without admin): WickedGL keeps its own setting */
	RegSetValueEx( k, name, 0, REG_DWORD, (const BYTE *)&v, sizeof( v ) );
	RegCloseKey( k );
}
void VID_GetMtGlTarget (char *dll, int size);
void VID_GetMtGlOptions (char *dll, char *sse, char *gdi, int size);

/* BDGN 3.58m: the 3dfx drivers read their swap interval (vsync) from the environment as they load; 1 when the
   loaded one was given vsync. The benchmark then restarts the renderer once (client/bench_config.h). */
static int vid_glide_swap;
qboolean VID_GlideVsyncOn (void)
{
	return vid_glide_swap != 0;
}

static void VID_3dfxSetup (void)
{
	const char *driver = Cvar_VariableString( "gl_driver" );
	char target[MAX_QPATH];
	int		tb = (int)Cvar_Get( "fx_triplebuffer", "0", CVAR_ARCHIVE )->value;
	int		hz = (int)Cvar_Get( "fx_refresh", "0", CVAR_ARCHIVE )->value;
	int		dither = (int)Cvar_Get( "fx_lod_dither", "0", CVAR_ARCHIVE )->value;
	int		fast = (int)Cvar_Get( "fx_fastmem", "0", CVAR_ARCHIVE )->value;
	int		filt = (int)Cvar_Get( "fx_videofilter", "1", CVAR_ARCHIVE )->value;
	int		cpu = (int)Cvar_Get( "fx_cpupath", "1", CVAR_ARCHIVE )->value;
	int		swap = (int)Cvar_Get( "gl_swapinterval", "0", CVAR_ARCHIVE )->value ? 1 : 0;
	char	n[16];
	swap = 0;	/* BDGN 3.58m: never vsync in a benchmark; 3.58o: never at all (owner, 2026-11-20) */
	vid_glide_swap = 0;
	if ( !Q_stricmp( driver, "mtgl32" ) )
	{
		VID_GetMtGlTarget( target, sizeof(target) );
		driver = target;
	}
	if ( Q_stricmp( driver, "3dfxgl" ) && Q_stricmp( driver, "3dfxgl.dll" ) &&
		 Q_stricmp( driver, "3dfxvgl" ) && Q_stricmp( driver, "3dfxvgl.dll" ) &&
		 Q_stricmp( driver, "Wicked3DFXgl" ) && Q_stricmp( driver, "Wicked3DFXgl.dll" ) &&
		 Q_stricmp( driver, "3dfxglam" ) && Q_stricmp( driver, "3dfxglam.dll" ) )
		return;
	VID_PutEnv( "FX_GLIDE_NO_SPLASH", "1" );
	VID_PutEnv( "FX_GLIDE_ALLOC_COLOR", tb ? "3" : "2" );
	VID_PutEnv( "SSTV2_ALLOC_COLOR", tb ? "3" : "2" );
	VID_PutEnv( "SSTH3_ALLOC_COLOR", tb ? "3" : "2" );
	VID_PutEnv( "FX_GLIDE_SWAPINTERVAL", swap ? "1" : "0" );
	VID_PutEnv( "SST_SWAPINTERVAL", swap ? "1" : "0" );
	VID_PutEnv( "SSTV2_SWAPINTERVAL", swap ? "1" : "0" );
	VID_PutEnv( "SSTH3_SWAPINTERVAL", swap ? "1" : "0" );
	vid_glide_swap = swap;
	Com_sprintf( n, sizeof( n ), "%i", hz );
	VID_PutEnv( "SST_SCREENREFRESH", hz ? n : NULL );
	VID_PutEnv( "SSTV2_SCREENREFRESH", hz ? n : NULL );
	VID_PutEnv( "SSTH3_SCREENREFRESH", hz ? n : NULL );
	VID_PutEnv( "FX_GLIDE_LOD_DITHER", dither ? "1" : "0" );
	VID_PutEnv( "SSTV2_FASTMEM_RAS_READS", fast ? "1" : NULL );
	VID_PutEnv( "SSTV2_FASTPCIRD", fast ? "1" : NULL );
	VID_PutEnv( "SSTV2_VIDEO_FILTER_DISABLE", filt ? NULL : "1" );
	VID_PutEnv( "GL_FORCE_K3D", cpu && Q3E_CPUHas3DNow() ? "1" : NULL );
	VID_PutEnv( "GL_FORCE_MMX", cpu && !Q3E_CPUHas3DNow() && Q3E_CPUHasMMX() ? "1" : NULL );
	if ( !Q_stricmp( driver, "Wicked3DFXgl" ) || !Q_stricmp( driver, "Wicked3DFXgl.dll" ) )
	{
		VID_WickedOption( "WickedGL_3B", tb );
		VID_WickedOption( "WickedGL_MipMap_Dither", dither );
	}
}

/*
BDGN 3.51ak: S3 Virge. The Techland "OpenGL-like driver for S3 Virge" (S3VirgeOpenGL.dll, beta 6, needs S3DTKW.dll from
the S3 driver) reads its options with GetPrivateProfileInt from OpenGL32.INI next to the DLL, section "Techland
OpenGL" (driver defaults in brackets): bDitherTexture [0], bSquareDown [0], bForceSquare [1], bSinglePrecisionMath [0],
nAlphaTreshold [80], bEmuPermedia [0], bVirge24BPP [0]. The menu rows set the s3v_* cvars; the file is written here,
before the renderer loads, whenever gl_driver names the S3 Virge driver. It only accepts 15/16-bit modes, so
gl_bitdepth 16 is set if it is 0 (desktop depth).
*/
static void VID_S3VirgeSetup( void )
{
	char	path[MAX_OSPATH], value[16], *slash;
	char	drv[64];
	int		i;

	strncpy( drv, Cvar_VariableString( "gl_driver" ), sizeof( drv ) - 1 );
	drv[sizeof( drv ) - 1] = 0;
	for ( i = 0; drv[i]; i++ )
		if ( drv[i] >= 'A' && drv[i] <= 'Z' )
			drv[i] += 'a' - 'A';
	if ( !strstr( drv, "s3virge" ) )
		return;
	if ( !Cvar_VariableValue( "gl_bitdepth" ) )
		Cvar_SetValue( "gl_bitdepth", 16 );
	if ( !GetModuleFileName( NULL, path, sizeof( path ) - 16 ) )
		return;
	slash = strrchr( path, '\\' );
	if ( !slash )
		return;
	strcpy( slash + 1, "OpenGL32.INI" );
#define S3V_INT(key, cvar, def) Com_sprintf( value, sizeof( value ), "%d", (int)Cvar_Get( cvar, def, CVAR_ARCHIVE )->value ); \
	WritePrivateProfileString( "Techland OpenGL", key, value, path )
	S3V_INT( "bDitherTexture", "s3v_dither", "0" );
	S3V_INT( "bSquareDown", "s3v_squaredown", "0" );
	S3V_INT( "bForceSquare", "s3v_forcesquare", "1" );
	S3V_INT( "bSinglePrecisionMath", "s3v_fastmath", "0" );
	S3V_INT( "nAlphaTreshold", "s3v_alpha", "80" );
#undef S3V_INT
	Com_Printf( "S3 Virge: options written to %s\n", path );
}

static qboolean vid_config_save_pending;
static qboolean vid_config_fallback;

void VID_RequestConfigSave (void)
{
 vid_config_save_pending = true;
}

/* BDGN 3.58r (owner's rule: the game remembers every setting): during a session that fell back to Software because the
   3D renderer would not start, config.cfg is written as well (before 3.58r nothing changed in that session was kept),
   with the renderer the player chose (VID_ConfigWriteBegin / End), so the next start tries it again. */
static char vid_fallback_ref[32];
qboolean VID_ConfigCanWrite (void)
{
 return !vid_config_save_pending;
}
void VID_ConfigWriteBegin (void)
{
 if (vid_config_fallback && vid_fallback_ref[0] && vid_ref)
 {
  Cvar_Set ("vid_ref", vid_fallback_ref);
  vid_ref->modified = false;
 }
}
void VID_ConfigWriteEnd (void)
{
 if (vid_config_fallback && vid_fallback_ref[0] && vid_ref)
 {
  Cvar_Set ("vid_ref", "soft");
  vid_ref->modified = false;
 }
}

qboolean VID_CommitConfigOnFrame (void)
{
 if (!vid_config_save_pending)
  return false;
 vid_config_save_pending = false;
 vid_config_fallback = false;
 return true;
}

static qboolean VID_ConfigureMtGlTarget(const char *dll,qboolean restart);

static qboolean VID_DriverMatches(const char *driver,const char *name,const char *filename)
{
 return !Q_stricmp(driver,name) || !Q_stricmp(driver,filename);
}

static qboolean VID_Is3dfxMtGlTarget(const char *target)
{
 return VID_DriverMatches(target,"3dfxgl","3dfxgl.dll") ||
        VID_DriverMatches(target,"3dfxvgl","3dfxvgl.dll") ||
        VID_DriverMatches(target,"3dfxglam","3dfxglam.dll") ||
        VID_DriverMatches(target,"Wicked3DFXgl","Wicked3DFXgl.dll");
}

static void VID_Log3dfxAttempt(const char *mode,const char *driver,const char *target,const char *stage)
{
 char path[MAX_OSPATH];
 FILE *file;
 Com_sprintf(path,sizeof(path),"%s/bdgn_3dfx_video.txt",FS_Gamedir());
 file=fopen(path,mode);
 if(!file) return;
 fprintf(file,"%s | driver=%s | MTGL target=%s | mode=%g | fullscreen=%g\n",
         stage,driver,target,Cvar_VariableValue("gl_mode"),Cvar_VariableValue("vid_fullscreen"));
 fclose(file);
}


/* BDGN 3.57o: simpler maps for the RENDERER ONLY (cl_simpleworld 1, bdgn_simpleworld.pak). maps/simple/<name>.bsp is the
   same map rebuilt without the detail nobody can stand on (wall and ceiling detail), with id's own light maps. Only the
   renderer gets it: collision, prediction, the map checksum the server checks and everything the game does use the
   original maps/<name>.bsp, so online play is unchanged (BDGN_RULES.md "Onlinespel"). maps/simple/<name>.hdr holds the
   original's length and BSP header; the simple map is used only when they match the map that is really loaded (a
   server's own map with the same name is drawn from its own file). The rebuilt map has one area, so the client draws it
   without area bits (vid_simpleworld_drawn, cl_view.c). */
qboolean vid_simpleworld_drawn;
static int VID_RendererLoadFile (char *path, void **buffer)
{
	size_t n = path ? strlen (path) : 0;
	if (n > 9 && !Q_strncasecmp (path, "maps/", 5) && !Q_stricmp (path + n - 4, ".bsp"))
	{
		vid_simpleworld_drawn = false;
		if (Cvar_VariableValue ("cl_simpleworld") && !Cvar_VariableValue ("fs_vanilla") &&
		    Q_strncasecmp (path + 5, "simple/", 7) && n < MAX_QPATH - 8)
		{
			char name[MAX_QPATH], simple[MAX_QPATH];
			byte *hdr = NULL, head[160];
			FILE *f = NULL;
			int hlen, olen;
			Com_sprintf (name, sizeof(name), "%s", path + 5);
			name[strlen (name) - 4] = 0;
			Com_sprintf (simple, sizeof(simple), "maps/simple/%s.hdr", name);
			hlen = FS_LoadFile (simple, (void **)&hdr);
			if (hdr && hlen == 4 + 160)
			{
				olen = FS_FOpenFile (path, &f);
				if (f)
				{
					if (olen >= 160)
						FS_Read (head, 160, f);
					fclose (f);
					if (olen >= 160 && LittleLong (*(int *)hdr) == olen && !memcmp (hdr + 4, head, 160))
					{
						int len;
						FS_FreeFile (hdr);
						Com_sprintf (simple, sizeof(simple), "maps/simple/%s.bsp", name);
						len = FS_LoadFile (simple, buffer);
						if (len > 0 && (!buffer || *buffer))
						{
							vid_simpleworld_drawn = buffer != NULL;
							Com_DPrintf ("simpler map for the renderer: %s\n", simple);
							return len;
						}
						return FS_LoadFile (path, buffer);
					}
				}
			}
			if (hdr)
				FS_FreeFile (hdr);
		}
	}
	return FS_LoadFile (path, buffer);
}

qboolean VID_LoadRefresh( char *name )
{
	refimport_t	ri;
	GetRefAPI_t	GetRefAPI;
	char mtgl_target[MAX_PATH], mtgl_sse[MAX_PATH], mtgl_gdi[MAX_PATH];
	
	if ( reflib_active )
	{
		re.Shutdown();
		VID_FreeReflib ();
	}

	Cvar_SetValue ("sw_bdgn_features", 0);
	Cvar_SetValue ("sw_smp_modes", 0);
	Cvar_SetValue ("sw_smp_quality", 0);
	Cvar_SetValue ("gl_render_scale_capable", 0);
	Cvar_SetValue ("bdgn_gl_anisotropy_max", 0);
	Cvar_SetValue ("bdgn_gl_hdtextures_capable", 0);
	Cvar_SetValue("bdgn_gl_soft_shadow_capable",0);
	Cvar_SetValue("bdgn_gl_water_surface_capable",0);
	Cvar_SetValue("bdgn_gl_water_reflect_capable",0);	/* BDGN 3.55o */
	Cvar_SetValue ("bdgn_gl_msaa_modes", 0);
	Cvar_SetValue ("bdgn_gl_msaa_samples", 0);
	Cvar_SetValue ("bdgn_gl_post_available", 0);
	Cvar_SetValue("bdgn_gl_pixel_lighting_capable",0);
	Cvar_SetValue("bdgn_gl_relief_capable",0);	/* BDGN 3.55ah */
	Cvar_SetValue("bdgn_gl_volume_shadow_capable",0);	/* BDGN 3.55ai */
	Cvar_SetValue("bdgn_gl_godrays_capable",0);	/* BDGN 3.55ak */
	Cvar_SetValue ("bdgn_gl_post_active", 0);
	Cvar_SetValue ("r_simd_sse2_mipmap", 0);
	Cvar_SetValue ("r_simd_sse2_lightmap", 0);
	Cvar_SetValue ("r_simd_sse_ready", 0);
	Cvar_SetValue ("r_simd_sse2_ready", 0);
	Cvar_SetValue ("r_simd_3dnow_ext", 0);
	Cvar_SetValue ("r_simd_3dnow_ext_ready", 0);
	Cvar_SetValue ("gl_render_scale_fbo_capable", 0);
	Cvar_SetValue ("gl_upscale_shader_capable", 0);
    Cvar_SetValue ("swam_bdgn_features", 0);
	Cvar_SetValue ("r_duke_anim_support", 0);
	Cvar_SetValue ("r_duke_res_avail", 0);
	Cvar_Set ("bdgn_gl_colorbits", "");
	Cvar_Set ("bdgn_gl_swapinterval", "");
	mtgl_target[0] = mtgl_sse[0] = mtgl_gdi[0] = 0;
	if ( !Q_stricmp( Cvar_VariableString("gl_driver"), "mtgl32" ) )
		VID_GetMtGlOptions( mtgl_target, mtgl_sse, mtgl_gdi, sizeof(mtgl_target) );
	Cvar_Set ("bdgn_mtgl_target", mtgl_target);
	Cvar_Set ("bdgn_mtgl_gdi", mtgl_gdi);
	Cvar_Set ("bdgn_mtgl_sse", mtgl_sse);
	Cvar_SetValue ("r_simpleenemy_bright", 0); /* Never reuse capability from the previous DLL. */
	Cvar_SetValue ("r_yshear_support", 0);	/* BDGN 3.60i: the Duke Nukem 3D view: ref_gl and ref_soft set it */
	Cvar_SetValue ("r_weaponsprite_support", 0);	/* BDGN 3.51ast: ref_v1k and other DLLs without the 2D weapon */
	Cvar_Set ("bdgn_gl_vendor", "");	Cvar_Set ("bdgn_gl_renderer", "");	Cvar_Set ("bdgn_gl_version", "");	/* BDGN 3.51p: set again by ref_gl */
	Cvar_SetValue ("glam_bdgn_features", 0); /* prevent a stale capability from another DLL */
	Com_Printf( "------- Loading %s -------\n", name );
	VID_S3VirgeSetup ();	/* BDGN 3.51ak */
	VID_3dfxSetup ();	/* BDGN 3.52l */

	Com_StartupMark ("renderer preflight");
	Cvar_Get ("bdgn_ref_init", "", 0);
	Cvar_Set ("bdgn_ref_init", "");	/* BDGN 3.59f: filled by ref_gl / ref_soft R_Init (not by AMD's ref_glam) */
	reflib_library = 0;
#ifdef BDGN_SSE_BUILD
	{	/* BDGN 3.60e: Quake2SSE.exe (Release_SSE) takes ref_<name>sse.dll first (ref_glsse.dll, ref_softsse.dll, built
		   with /arch:SSE), else the normal DLL (AMD's ref_glam / ref_swam, ref_v1k have no SSE version) */
		char ssename[MAX_OSPATH];
		const char *dot = strrchr( name, '.' );
		if ( dot && (int)(dot - name) + 8 < (int)sizeof(ssename) )
		{
			memcpy( ssename, name, dot - name );
			strcpy( ssename + (dot - name), "sse.dll" );
			if ( ( reflib_library = LoadLibrary( ssename ) ) != 0 )
				Com_Printf( "SSE renderer: %s\n", ssename );
		}
	}
	if ( !reflib_library )
#endif
	reflib_library = LoadLibrary( name );
	if ( reflib_library == 0 )
	{
		Com_StartupMark ("renderer DLL load failed");
		Com_Printf( "LoadLibrary(\"%s\") failed\n", name );

		return false;
	}

	Com_StartupMark ("renderer DLL loaded");
	ri.Cmd_AddCommand = Cmd_AddCommand;
	ri.Cmd_RemoveCommand = Cmd_RemoveCommand;
	ri.Cmd_Argc = Cmd_Argc;
	ri.Cmd_Argv = Cmd_Argv;
	ri.Cmd_ExecuteText = Cbuf_ExecuteText;
	ri.Con_Printf = VID_Printf;
	ri.Sys_Error = VID_Error;
	ri.FS_LoadFile = VID_RendererLoadFile;	/* BDGN 3.57o: simpler map for the renderer only (cl_simpleworld) */
	ri.FS_FreeFile = FS_FreeFile;
	ri.FS_Gamedir = FS_Gamedir;
	ri.Cvar_Get = Cvar_Get;
	ri.Cvar_Set = Cvar_Set;
	ri.Cvar_SetValue = Cvar_SetValue;
	ri.Vid_GetModeInfo = VID_GetModeInfo;
	ri.Vid_MenuInit = VID_MenuInit;
	ri.Vid_NewWindow = VID_NewWindow;

	if ( ( GetRefAPI = (void *) GetProcAddress( reflib_library, "GetRefAPI" ) ) == 0 )
		Com_Error( ERR_FATAL, "GetProcAddress failed on %s", name );

	re = GetRefAPI( ri );
	CL_FXB_Hook ();	/* BDGN 3.56s: effect sizes for the wall check (client/cl_view.c) */

	if (re.api_version != API_VERSION)
	{
		VID_FreeReflib ();
		Com_Error (ERR_FATAL, "%s has incompatible api_version", name);
	}

	Com_StartupMark ("renderer API ready");
	if ( re.Init( global_hInstance, MainWndProc ) == -1 )
	{
		Com_StartupMark ("renderer initialization failed");
		re.Shutdown();
		VID_FreeReflib ();
		return false;
	}

	Com_StartupMark ("renderer initialized");
	native_menu_begin=(void (*)(void))GetProcAddress(reflib_library,"SWimp_MenuBegin");
	native_menu_end=(void (*)(void))GetProcAddress(reflib_library,"SWimp_MenuEnd");
	Com_Printf( "------------------------------------\n");
	reflib_active = true;

//======
//PGM
	vidref_val = VIDREF_OTHER;
	if(vid_ref)
	{
		if(!strcmp (vid_ref->string, "gl") || !strcmp (vid_ref->string, "glam"))
			vidref_val = VIDREF_GL;
		else if(!strcmp(vid_ref->string, "soft") || !strcmp(vid_ref->string,"swam"))
			vidref_val = VIDREF_SOFT;
	}
//PGM
//======

	return true;
}

/*
============
VID_CheckChanges

This function gets called once just before drawing each frame, and it's sole purpose in life
is to check to see if any of the video mode parameters have changed, and if they have to 
update the rendering DLL and/or video mode to match.
============
*/
void VID_CheckChanges (void)
{
	char name[100];

	if ( scr_scale->modified )
	{
		scr_scale->modified = false;
		VID_ApplyScale ();
	}

	if ( win_noalttab->modified )
	{
		if ( win_noalttab->value )
		{
			WIN_DisableAltTab();
		}
		else
		{
			WIN_EnableAltTab();
		}
		win_noalttab->modified = false;
	}

	if ( vid_ref->modified )
	{
		cl.force_refdef = true;		// can't use a paused refdef
		S_StopAllSounds();
	}
	while (vid_ref->modified)
	{
        char driver[MAX_QPATH], target[MAX_PATH]="";
        qboolean tracked, loaded;
        Com_sprintf(driver,sizeof(driver),"%s",Cvar_VariableString("gl_driver"));
        if(Q_stricmp(vid_ref->string,"gl") && Q_stricmp(vid_ref->string,"glam"))
            driver[0]=0;
        if(VID_DriverMatches(driver,"mtgl32","mtgl32.dll"))
        {
            VID_GetMtGlTarget(target,sizeof(target));
            if(!target[0]) Com_sprintf(target,sizeof(target),"%s","opengl32.dll");
        }
        tracked=VID_Is3dfxMtGlTarget(target) ||
                VID_DriverMatches(driver,"Wicked3DFXgl","Wicked3DFXgl.dll");
		/*
		** refresh has changed
		*/
		vid_ref->modified = false;
		vid_fullscreen->modified = true;
		cl.refresh_prepped = false;
		cls.disable_screen = true;

		VID_ApplyLegacyFidelity ();
		Com_sprintf( name, sizeof(name), "ref_%s.dll", vid_ref->string );
        if(tracked) VID_Log3dfxAttempt("w",driver,target,"renderer switch started");
        loaded=false;
        if(VID_DriverMatches(target,"opengl32","opengl32.dll") &&
                Cvar_VariableValue("vid_rendition") && !Cvar_VariableValue("bdgn_rendition_smp_test"))
        {
            Com_Printf("Rendition OpenGL SMP is disabled by default (experimental).\n");
        }
        else if(target[0] && !VID_ConfigureMtGlTarget(target,false))
        {
            if(tracked) VID_Log3dfxAttempt("a",driver,target,"mt_gl.ini validation failed");
        }
        else
        {
            loaded=VID_LoadRefresh(name);
            if(tracked) VID_Log3dfxAttempt("a",driver,target,
                loaded?"renderer initialized":"renderer initialization failed");
        }
		/* BDGN 3.57e: before giving up on OpenGL, try once more in a mode every card of the time has: 640x480,
		   no render scaling, no MSAA. Before 3.57e a saved mode the card or driver refused (or a setting of a newer
		   computer in the same config.cfg) made every start fall back to Software, although OpenGL worked when it
		   was chosen again in the menu. The working settings are saved after the first frame. */
		if ( !loaded && (!Q_stricmp (vid_ref->string, "gl") || !Q_stricmp (vid_ref->string, "glam")) && !CL_BenchmarkRunning () )
		{
			char oldmode[16];
			Com_sprintf (oldmode, sizeof(oldmode), "%s", Cvar_VariableString ("gl_mode"));
			Com_Printf ("Video: %s failed in mode %s; trying again in 640x480 without scaling or MSAA\n", name, oldmode);
			Com_StartupMark ("OpenGL retry in 640x480");
			Cvar_Set ("gl_mode", "3");
			Cvar_Set ("gl_render_scale", "100");
			Cvar_Set ("gl_ext_samples", "0");
			loaded = VID_LoadRefresh (name);
			if ( loaded )
			{
				Com_Printf ("Video: %s works in 640x480 (gl_mode 3); the video mode can be raised again in the menu\n", name);
				vid_config_save_pending = true;	/* write the working settings after the first frame */
			}
			else
				Cvar_Set ("gl_mode", oldmode);
		}
		if ( !loaded )
		{
			if ( strcmp (vid_ref->string, "soft") == 0 )
				Com_Error (ERR_FATAL, "Couldn't fall back to software refresh!");
			Com_StartupFallback ("OpenGL/3D renderer failed: using Software");
			Com_Printf ("Video: %s failed with driver %s; using Software for this session. Saved settings kept.\n",
				name, Cvar_VariableString("gl_driver"));
			if (!CL_BenchmarkRunning ())
			{
				vid_config_fallback = true;
				Com_sprintf (vid_fallback_ref, sizeof(vid_fallback_ref), "%s", vid_ref->string);	/* 3.58r */
			}
			vid_config_save_pending = false;
			Cvar_Set( "vid_ref", "soft" );

			/*
			** drop the console if we fail to load a refresh
			*/
			if ( cls.key_dest != key_console )
			{
				Con_ToggleConsole_f();
			}
		}
		cls.disable_screen = false;
	}

	/*
	** update our window position
	*/
	if ( vid_xpos->modified || vid_ypos->modified )
	{
		if (!vid_fullscreen->value)
			VID_UpdateWindowPosAndSize( vid_xpos->value, vid_ypos->value );

		vid_xpos->modified = false;
		vid_ypos->modified = false;
	}
}

/*
============
VID_Init
============
*/
void VID_Init (void)
{
	/* Create the video variables so we know how to start the graphics drivers */
	vid_ref = Cvar_Get ("vid_ref", "soft", CVAR_ARCHIVE);
	Cvar_Get ("gl_driver", "opengl32", CVAR_ARCHIVE);
	scr_scale = Cvar_Get ("scr_scale", "0", CVAR_ARCHIVE);	// BDGN 3.50: menu/HUD scale, 0 = auto
	Cvar_Get ("scr_scale_active", "1", 0);
	vid_xpos = Cvar_Get ("vid_xpos", "3", CVAR_ARCHIVE);
	vid_ypos = Cvar_Get ("vid_ypos", "22", CVAR_ARCHIVE);
	vid_fullscreen = Cvar_Get ("vid_fullscreen", "0", CVAR_ARCHIVE);
	vid_gamma = Cvar_Get( "vid_gamma", "1", CVAR_ARCHIVE );
	win_noalttab = Cvar_Get( "win_noalttab", "0", CVAR_ARCHIVE );

	/* Add some console commands that we want to handle */
	Cmd_AddCommand ("vid_restart", VID_Restart_f);
	Cmd_AddCommand ("vid_defaults", VID_DefaultConfig_f);
	Cmd_SetPresetExecHandler (VID_ExecPreset);
	Cmd_AddCommand ("vid_front", VID_Front_f);
	Cmd_AddCommand ("bdgn_hardware", VID_Hardware_f);	/* BDGN 3.52k */

	/*
	** this is a gross hack but necessary to clamp the mode for 3Dfx
	*/
#if 0
	{
		cvar_t *gl_driver = Cvar_Get( "gl_driver", "opengl32", 0 );
		cvar_t *gl_mode = Cvar_Get( "gl_mode", "3", 0 );

		if ( stricmp( gl_driver->string, "3dfxgl" ) == 0 )
		{
			Cvar_SetValue( "gl_mode", 3 );
			viddef.width  = 640;
			viddef.height = 480;
		}
	}
#endif

	/* Disable the 3Dfx splash screen */
	putenv("FX_GLIDE_NO_SPLASH=0");
		
	/* Start the graphics mode and load refresh DLL */
	VID_CheckChanges();
}

/* BDGN 3.52k: graphics hardware detection for the video menu (which drivers and presets are offered).
   The PCI devices Windows knows are read from the registry: HKLM\SYSTEM\CurrentControlSet\Enum\PCI (NT/2000/XP and
   later) or HKLM\Enum\PCI (Windows 95/98/ME). A device is recognised by its PCI vendor/device id (key name
   "VEN_xxxx&DEV_yyyy...") or by words in its DeviceDesc. Windows keeps entries of cards that were removed, so an old
   card can still count; "bdgn_hw_all 1" (saved) shows every driver and preset anyway. Result: cvar bdgn_hw (bits
   below, not saved) and the console command bdgn_hardware. */
#define HW_V1		1		/* 3dfx Voodoo Graphics (121A:0001) */
#define HW_V2		2		/* 3dfx Voodoo2 (121A:0002) */
#define HW_BANSHEE	4		/* 3dfx Voodoo Banshee (121A:0003, 0004) */
#define HW_V3		8		/* 3dfx Voodoo3 (121A:0005, 0057) */
#define HW_V45		16		/* 3dfx Voodoo4 / Voodoo5 (121A:0007, 0009) */
#define HW_RUSH		32		/* 3dfx Voodoo Rush (DeviceDesc) */
#define HW_PVR		64		/* PowerVR: NEC PCX1/PCX2/Neon 250 (1033:0046, 0067, 0120), Kyro (104A:0010) */
#define HW_S3V		128		/* S3 ViRGE family (5333:5631, 883D, 8A01, 8A10, 8C01-8C03) */
#define HW_V1000	256		/* Rendition Verite V1000 (1163:0001) */
#define HW_V2X00	512		/* Rendition Verite V2100/V2200 (1163:2000) */
/* BDGN 3.52m: cards on the system OpenGL driver (ICD), for their own presets */
#define HW_RIVA		1024	/* NVIDIA Riva 128 / TNT / TNT2 / Vanta (12D2:0018-0019, 10DE:0020, 0028-002D, 00A0) */
#define HW_MGA		2048	/* Matrox G200 / G400 / G450 (102B:0520, 0521, 0525) */
#define HW_RAGE		4096	/* ATI Rage II / IIC / Pro / LT / 128 (1002, DeviceDesc "rage") */
#define HW_I740		8192	/* Intel i740 (8086:7800) */
#define HW_SAVAGE	16384	/* S3 Savage3D / Savage4 / Savage2000 (5333:8A20-8A22, 9102) */
#define HW_LOWEND	32768	/* SiS 6326, Trident 3DImage, Cirrus Laguna, Matrox Mystique/G100, 3Dlabs Permedia */
#define HW_GF23		65536	/* BDGN 3.58d: NVIDIA GeForce2 GTS/Pro/Ultra/MX and GeForce3 (10DE:0110-0113, 0150-0153, 0200-0203) */
#define HW_ALL		131071
static int vid_hw = -1;
static const char *vid_hw_names[] = { "3dfx Voodoo Graphics", "3dfx Voodoo2", "3dfx Voodoo Banshee", "3dfx Voodoo3",
	"3dfx Voodoo4/5", "3dfx Voodoo Rush", "PowerVR", "S3 ViRGE", "Rendition V1000", "Rendition V2x00",
	"NVIDIA Riva 128 / TNT", "Matrox G200/G400", "ATI Rage", "Intel i740", "S3 Savage", "budget 3D (SiS / Trident / Cirrus / Mystique / Permedia)", "NVIDIA GeForce2 / GeForce3" };
static int VID_PciId (unsigned ven, unsigned dev)
{
	if ( ven == 0x121A )
	{
		if ( dev == 0x0001 ) return HW_V1;
		if ( dev == 0x0002 ) return HW_V2;
		if ( dev == 0x0003 || dev == 0x0004 ) return HW_BANSHEE;
		if ( dev == 0x0005 || dev == 0x0057 ) return HW_V3;
		if ( dev == 0x0007 || dev == 0x0009 ) return HW_V45;
		return 0;
	}
	if ( ven == 0x1033 && ( dev == 0x0046 || dev == 0x0067 || dev == 0x0120 ) ) return HW_PVR;
	if ( ven == 0x104A && dev == 0x0010 ) return HW_PVR;
	if ( ven == 0x5333 && ( dev == 0x5631 || dev == 0x883D || dev == 0x8A01 || dev == 0x8A10 || ( dev >= 0x8C01 && dev <= 0x8C03 ) ) ) return HW_S3V;
	if ( ven == 0x1163 && dev == 0x0001 ) return HW_V1000;
	if ( ven == 0x1163 && dev == 0x2000 ) return HW_V2X00;
	if ( ( ven == 0x12D2 && ( dev == 0x0018 || dev == 0x0019 ) ) || ( ven == 0x10DE && ( dev == 0x0020 || ( dev >= 0x0028 && dev <= 0x002D ) || dev == 0x00A0 ) ) ) return HW_RIVA;
	if ( ven == 0x10DE && ( ( dev >= 0x0110 && dev <= 0x0113 ) || ( dev >= 0x0150 && dev <= 0x0153 ) || ( dev >= 0x0200 && dev <= 0x0203 ) ) ) return HW_GF23;
	if ( ven == 0x102B && ( dev == 0x0520 || dev == 0x0521 || dev == 0x0525 ) ) return HW_MGA;
	if ( ven == 0x102B && ( dev == 0x051A || dev == 0x051B || dev == 0x051F || dev == 0x1000 || dev == 0x1001 ) ) return HW_LOWEND;
	if ( ven == 0x8086 && dev == 0x7800 ) return HW_I740;
	if ( ven == 0x5333 && ( ( dev >= 0x8A20 && dev <= 0x8A22 ) || dev == 0x9102 ) ) return HW_SAVAGE;
	if ( ven == 0x1039 && dev == 0x6326 ) return HW_LOWEND;
	if ( ven == 0x1023 && ( dev == 0x9750 || dev == 0x9850 ) ) return HW_LOWEND;
	if ( ven == 0x1013 && ( dev == 0x00D0 || dev == 0x00D4 || dev == 0x00D6 ) ) return HW_LOWEND;
	if ( ven == 0x3D3D || ( ven == 0x104C && dev == 0x3D07 ) ) return HW_LOWEND;
	return 0;
}
static int VID_DescId (const char *d)
{
	char s[256];
	int i;
	for ( i = 0; d[i] && i < 255; i++ ) s[i] = (char)tolower( (unsigned char)d[i] );
	s[i] = 0;
	if ( strstr( s, "voodoo rush" ) ) return HW_RUSH;
	if ( strstr( s, "voodoo2" ) || strstr( s, "voodoo 2" ) ) return HW_V2;
	if ( strstr( s, "banshee" ) ) return HW_BANSHEE;
	if ( strstr( s, "voodoo3" ) || strstr( s, "voodoo 3" ) ) return HW_V3;
	if ( strstr( s, "voodoo4" ) || strstr( s, "voodoo5" ) || strstr( s, "voodoo 4" ) || strstr( s, "voodoo 5" ) ) return HW_V45;
	if ( strstr( s, "voodoo" ) ) return HW_V1;
	if ( strstr( s, "powervr" ) || strstr( s, "pcx1" ) || strstr( s, "pcx2" ) || strstr( s, "neon 250" ) || strstr( s, "kyro" ) ) return HW_PVR;
	if ( strstr( s, "virge" ) ) return HW_S3V;
	if ( strstr( s, "v2100" ) || strstr( s, "v2200" ) || strstr( s, "v2x00" ) ) return HW_V2X00;
	if ( strstr( s, "verite" ) || strstr( s, "rendition" ) ) return HW_V1000;
	if ( strstr( s, "riva" ) || strstr( s, "tnt" ) || strstr( s, "vanta" ) ) return HW_RIVA;
	if ( strstr( s, "geforce2" ) || strstr( s, "geforce 2" ) || strstr( s, "geforce3" ) || strstr( s, "geforce 3" ) ) return HW_GF23;
	if ( strstr( s, "g200" ) || strstr( s, "g400" ) || strstr( s, "g450" ) ) return HW_MGA;
	if ( ( strstr( s, " rage" ) || !strncmp( s, "rage", 4 ) ) && !strstr( s, "radeon" ) ) return HW_RAGE;	/* not "storage" */
	if ( strstr( s, "i740" ) || strstr( s, "intel740" ) || strstr( s, "intel 740" ) ) return HW_I740;
	if ( strstr( s, "savage" ) ) return HW_SAVAGE;
	if ( strstr( s, "6326" ) || strstr( s, "3dimage" ) || strstr( s, "laguna" ) || strstr( s, "mystique" ) || strstr( s, "permedia" ) || strstr( s, "g100" ) ) return HW_LOWEND;
	return 0;
}
static int VID_ScanPci (const char *root)
{
	HKEY	k, dk, ik;
	char	name[256], inst[256], desc[256];
	DWORD	i, j, n, type, len;
	unsigned ven, dev;
	int		hw = 0;
	if ( RegOpenKeyEx( HKEY_LOCAL_MACHINE, root, 0, KEY_READ, &k ) != ERROR_SUCCESS )
		return 0;
	for ( i = 0; ; i++ )
	{
		n = sizeof( name );
		if ( RegEnumKeyEx( k, i, name, &n, NULL, NULL, NULL, NULL ) != ERROR_SUCCESS )
			break;
		_strupr( name );
		if ( sscanf( name, "VEN_%4x&DEV_%4x", &ven, &dev ) == 2 )
			hw |= VID_PciId( ven, dev );
		if ( RegOpenKeyEx( k, name, 0, KEY_READ, &dk ) != ERROR_SUCCESS )
			continue;
		for ( j = 0; j < 8; j++ )
		{
			n = sizeof( inst );
			if ( RegEnumKeyEx( dk, j, inst, &n, NULL, NULL, NULL, NULL ) != ERROR_SUCCESS )
				break;
			if ( RegOpenKeyEx( dk, inst, 0, KEY_READ, &ik ) != ERROR_SUCCESS )
				continue;
			len = sizeof( desc ) - 1;
			if ( RegQueryValueEx( ik, "DeviceDesc", NULL, &type, (LPBYTE)desc, &len ) == ERROR_SUCCESS && type == REG_SZ )
			{
				desc[len < sizeof( desc ) ? len : sizeof( desc ) - 1] = 0;
				hw |= VID_DescId( desc );
			}
			RegCloseKey( ik );
		}
		RegCloseKey( dk );
	}
	RegCloseKey( k );
	return hw;
}
int VID_HardwareFlags (void)
{
	if ( vid_hw < 0 )
	{
		vid_hw = VID_ScanPci( "SYSTEM\\CurrentControlSet\\Enum\\PCI" ) | VID_ScanPci( "Enum\\PCI" );
		Cvar_FullSet( "bdgn_hw", va( "%i", vid_hw ), 0 );
	}
	return Cvar_Get( "bdgn_hw_all", "0", CVAR_ARCHIVE )->value ? HW_ALL : vid_hw;
}
/*
================
BDGN 3.55q: names from the driver, for the automatic preset (client/cl_main.c CL_AutoPresetChoose)

VID_AdapterName: the primary display adapter as the driver names it (EnumDisplayDevices; Windows 98/ME/2000 and
newer, looked up at run time so Windows 95 still starts), else "".
VID_CpuName: ProcessorNameString from the registry (Windows NT family), else "".
VID_DriverYear: newest DriverDate of the display drivers (Class {4d36e968...} on NT, Services\Class\Display on 9x),
0 when none is found.
VID_DesktopSize: the desktop resolution (the resolution a player normally ends up using).
================
*/
typedef struct { DWORD cb; char DeviceName[32]; char DeviceString[128]; DWORD StateFlags; char DeviceID[128]; char DeviceKey[128]; } bdgn_display_device_t;
typedef BOOL (WINAPI *bdgn_enum_display_fn)(LPCSTR,DWORD,bdgn_display_device_t *,DWORD);
void VID_AdapterName (char *out, int size)
{
	HMODULE user = GetModuleHandle( "user32.dll" );
	bdgn_enum_display_fn enum_devices = user ? (bdgn_enum_display_fn)GetProcAddress( user, "EnumDisplayDevicesA" ) : NULL;
	bdgn_display_device_t dd;
	DWORD i;
	out[0] = 0;
	if ( !enum_devices ) return;
	for ( i = 0; i < 16; i++ )
	{
		memset( &dd, 0, sizeof(dd) );
		dd.cb = sizeof(dd);
		if ( !enum_devices( NULL, i, &dd, 0 ) ) break;
		if ( !dd.DeviceString[0] ) continue;
		if ( !out[0] || ( dd.StateFlags & 4 ) )	/* DISPLAY_DEVICE_PRIMARY_DEVICE */
			{ strncpy( out, dd.DeviceString, size - 1 ); out[size - 1] = 0; }
		if ( dd.StateFlags & 4 ) break;
	}
}

void VID_CpuName (char *out, int size)
{
	HKEY k;
	DWORD type, len = size;
	out[0] = 0;
	if ( RegOpenKeyEx( HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &k ) != ERROR_SUCCESS )
		return;
	if ( RegQueryValueEx( k, "ProcessorNameString", NULL, &type, (LPBYTE)out, &len ) != ERROR_SUCCESS || type != REG_SZ )
		out[0] = 0;
	out[size - 1] = 0;
	RegCloseKey( k );
}

static int VID_DriverYearIn (const char *path)
{
	HKEY k, dk;
	char sub[16], date[64];
	DWORD i, len, type;
	int best = 0, y;
	if ( RegOpenKeyEx( HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k ) != ERROR_SUCCESS )
		return 0;
	for ( i = 0; i < 16; i++ )
	{
		Com_sprintf( sub, sizeof(sub), "%04i", (int)i );
		if ( RegOpenKeyEx( k, sub, 0, KEY_READ, &dk ) != ERROR_SUCCESS ) continue;
		len = sizeof(date);
		if ( RegQueryValueEx( dk, "DriverDate", NULL, &type, (LPBYTE)date, &len ) == ERROR_SUCCESS && type == REG_SZ )
		{	/* "m-d-yyyy" */
			const char *p = strrchr( date, '-' );
			date[sizeof(date) - 1] = 0;
			y = p ? atoi( p + 1 ) : 0;
			if ( y > best && y < 3000 ) best = y;
		}
		RegCloseKey( dk );
	}
	RegCloseKey( k );
	return best;
}

int VID_DriverYear (void)
{
	int a = VID_DriverYearIn( "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}" );
	int b = VID_DriverYearIn( "System\\CurrentControlSet\\Services\\Class\\Display" );
	return a > b ? a : b;
}

void VID_DesktopSize (int *w, int *h)
{
	*w = GetSystemMetrics( SM_CXSCREEN );
	*h = GetSystemMetrics( SM_CYSCREEN );
}

static void VID_Hardware_f (void)
{
	int i, n, s, hw;
	vid_hw = -1;
	hw = VID_HardwareFlags();
	Sys_ProcessorCounts( &s, &n );
	Com_Printf( "BDGN hardware: %i processor(s) for the game (%i in the system); MMX %s, 3DNow! %s, SSE %s\n", n, s,
		Q3E_CPUHasMMX() ? "yes" : "no", Q3E_CPUHas3DNow() ? "yes" : "no", Q3E_CPUHasSSE() ? "yes" : "no" );
	for ( i = 0; i < 17; i++ )	/* 3.58d: 17 with GeForce2/3 */
		if ( vid_hw & ( 1 << i ) )
			Com_Printf( "  found: %s\n", vid_hw_names[i] );
	if ( !vid_hw )
		Com_Printf( "  no card with its own drivers or presets found (generic OpenGL presets)\n" );
	if ( hw != vid_hw )
		Com_Printf( "  bdgn_hw_all 1: every driver and preset is shown\n" );
}

/* MTGL reads mt_gl.ini next to its own module during DLL initialization. */
static qboolean VID_MtGlIniPath (char *path, DWORD size)
{
	static const char ini_name[] = "mt_gl.ini";
	HMODULE module = GetModuleHandle( "mtgl32.dll" );
	DWORD length = module ? GetModuleFileName( module, path, size ) :
		SearchPath( NULL, "mtgl32.dll", NULL, size, path, NULL );
	char *name;

	if ( !length || length >= size ) return false;
	name = strrchr( path, '\\' );
	if ( !name || (size_t)(++name - path) + sizeof(ini_name) > size ) return false;
	memcpy( name, ini_name, sizeof(ini_name) );
	return true;
}
qboolean VID_WriteMtGlOptions (const char *dll, const char *sse, const char *gdi)
{
	char ini[MAX_PATH + 1];
	BOOL ok;

	if ( !VID_MtGlIniPath( ini, sizeof(ini) ) ) {
		Com_Printf( "MTGL: could not locate mt_gl.ini beside mtgl32.dll\n" );
		return false;
	}
	ok = WritePrivateProfileString( "Main", "OpenGL32", dll[0] ? dll : NULL, ini );
	ok &= WritePrivateProfileString( "CPU", "UseSSE", sse[0] ? sse : NULL, ini );
	ok &= WritePrivateProfileString( "Main", "DoGDICalls", gdi[0] ? gdi : NULL, ini );
	if ( !ok ) Com_Printf( "MTGL: could not update %s\n", ini );
	return ok != 0;
}
void VID_GetMtGlOptions (char *dll, char *sse, char *gdi, int size)
{
	char ini[MAX_PATH + 1];
	dll[0] = sse[0] = gdi[0] = 0;
	if ( !VID_MtGlIniPath( ini, sizeof(ini) ) ) return;
	GetPrivateProfileString( "Main", "OpenGL32", "", dll, size, ini );
	GetPrivateProfileString( "CPU", "UseSSE", "", sse, size, ini );
	GetPrivateProfileString( "Main", "DoGDICalls", "", gdi, size, ini );
}
const char *VID_MtGlGdiValue (const char *dll)
{
	return !Q_stricmp( dll, "opengl32.dll" ) ? "1" : "0";
}
static qboolean VID_ConfigureMtGlTarget(const char *dll,qboolean restart)
{
	char ini[MAX_PATH+1], old_dll[MAX_PATH], old_sse[MAX_PATH], old_gdi[MAX_PATH];
	char current_dll[MAX_PATH], current_sse[MAX_PATH], current_gdi[MAX_PATH];
	const char *sse=Q3E_CPUHasSSE()?"1":"0";
	const char *gdi=VID_MtGlGdiValue(dll);
	qboolean changed;

	if(!VID_MtGlIniPath(ini,sizeof(ini)))
	{
		Com_Printf("MTGL: could not locate mt_gl.ini beside mtgl32.dll.\n");
		return false;
	}
	changed=!Q3E_CPUHasSSE2() && GetPrivateProfileInt("CPU","UseSSE2",0,ini)!=0;
	if(changed && (!WritePrivateProfileString("CPU","UseSSE2","0",ini) ||
	              GetPrivateProfileInt("CPU","UseSSE2",0,ini)!=0))
	{
		Com_Printf("MTGL: could not disable unsupported SSE2 in %s.\n",ini);
		return false;
	}
	VID_GetMtGlOptions(old_dll,old_sse,old_gdi,sizeof(old_dll));
	if(Q_stricmp(old_dll,dll) || strcmp(old_sse,sse) || strcmp(old_gdi,gdi))
	{
		qboolean written=VID_WriteMtGlOptions(dll,sse,gdi);
		VID_GetMtGlOptions(current_dll,current_sse,current_gdi,sizeof(current_dll));
		if(!written || Q_stricmp(current_dll,dll) || strcmp(current_sse,sse) || strcmp(current_gdi,gdi))
		{
			if(Q_stricmp(current_dll,old_dll) || strcmp(current_sse,old_sse) || strcmp(current_gdi,old_gdi))
				VID_WriteMtGlOptions(old_dll,old_sse,old_gdi);
			Com_Printf("MTGL: configuration for %s could not be verified.\n",dll);
			return false;
		}
		changed=true;
	}
	if(changed && restart)
		vid_ref->modified=true; /* The gl_driver string is still "mtgl32". */
	return true;
}

qboolean VID_SetMtGlTarget(const char *dll)
{
	return VID_ConfigureMtGlTarget(dll,true);
}
void VID_GetMtGlTarget (char *dll, int size)
{
	char ini[MAX_PATH + 1];
	if ( VID_MtGlIniPath( ini, sizeof(ini) ) )
		GetPrivateProfileString( "Main", "OpenGL32", "opengl32.dll", dll, size, ini );
	else
		Com_sprintf( dll, size, "%s", "opengl32.dll" );
}

/* BDGN 3.52i: 1 when Windows finds NAME.dll the way LoadLibrary does (game folder, system folders, PATH) */
int VID_DriverDllFound (const char *name)
{
	char	file[MAX_OSPATH], path[MAX_OSPATH], *part;
	Com_sprintf( file, sizeof( file ), "%s.dll", name );
	return SearchPath( NULL, file, NULL, sizeof( path ), path, &part ) != 0;
}

/*
============
VID_Shutdown
============
*/
void VID_Shutdown (void)
{
	if ( reflib_active )
	{
		re.Shutdown ();
		VID_FreeReflib ();
	}
}


