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
// sys_win.h

#include "../qcommon/qcommon.h"
#include "winquake.h"
#include "resource.h"
#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>
#include "../win32/conproc.h"

#define MINIMUM_WIN_MEMORY	0x0a00000
#define MAXIMUM_WIN_MEMORY	0x1000000

//#define DEMO

qboolean s_win95;

int			starttime;
qboolean	ActiveApp;
qboolean	Minimized;

static HANDLE		hinput, houtput;

unsigned	sys_msg_time;
unsigned	sys_frame_time;


static HANDLE		qwclsemaphore;

#define	MAX_NUM_ARGVS	128
int			argc;
char		*argv[MAX_NUM_ARGVS];


/*
===============================================================================

SYSTEM IO

===============================================================================
*/


/* A marker is written only when we are about to run a loose config. The
   exclusive handle also prevents a second starting client from moving it. */
static HANDLE startup_config_lock = INVALID_HANDLE_VALUE;
static char startup_config_marker[MAX_OSPATH];
static qboolean startup_config_recovering;
static qboolean startup_config_regenerate;
static qboolean startup_config_readonly;

typedef struct {
	DWORD magic;
	DWORD length;
	DWORD hash;
} startup_config_t;

static qboolean Sys_StartupConfigStamp (const char *path, startup_config_t *stamp)
{
	unsigned char buf[4096];
	size_t i, count;
	qboolean ok;
	FILE *file = fopen (path, "rb");
	if (!file)
		return false;
	stamp->magic = 0x43474631;
	stamp->length = 0;
	stamp->hash = 2166136261UL;
	while ((count = fread (buf, 1, sizeof(buf), file)) != 0)
	{
		stamp->length += (DWORD)count;
		for (i = 0; i < count; i++)
			stamp->hash = (stamp->hash ^ buf[i]) * 16777619UL;
	}
	ok = !ferror (file);
	fclose (file);
	return ok;
}

static qboolean Sys_BackupStartupConfig (const char *path)
{
	char backup[MAX_OSPATH];
	int i;
	for (i = 1; i <= 999; i++)
	{
		Com_sprintf (backup, sizeof(backup), "%s/config.failed-%03i.cfg", FS_Gamedir(), i);
		if (MoveFile (path, backup))
		{
			Com_Printf ("Startup recovery: saved %s\n", backup);
			return true;
		}
		if (GetFileAttributes (backup) == INVALID_FILE_ATTRIBUTES)
			break;
	}
	Com_Printf ("Startup recovery: could not back up config.cfg. It will be left intact.\n");
	return false;
}

qboolean Sys_StartupConfigBegin (void)
{
	char path[MAX_OSPATH];
	startup_config_t current, previous;
	DWORD count;
	qboolean has_config;

	Com_sprintf (path, sizeof(path), "%s/config.cfg", FS_Gamedir());
	Com_sprintf (startup_config_marker, sizeof(startup_config_marker),
		"%s/bdgn_startup.pending", FS_Gamedir());
	startup_config_lock = CreateFile (startup_config_marker,
		GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	count = GetLastError ();
	has_config = Sys_StartupConfigStamp (path, &current);
	if (startup_config_lock == INVALID_HANDLE_VALUE)
	{
		/* Even without a marker, respect the saved configuration. Avoid
		   overwriting it if another startup has the marker locked. */
		startup_config_readonly = count == ERROR_SHARING_VIOLATION;
		startup_config_regenerate = !has_config && !startup_config_readonly;
		return has_config;
	}
	if (has_config && ReadFile (startup_config_lock, &previous, sizeof(previous), &count, NULL)
		&& count == sizeof(previous) && previous.magic == current.magic
		&& previous.length == current.length && previous.hash == current.hash)
	{
		startup_config_recovering = true;
		Com_StartupFallback ("unfinished startup: using default.cfg");
		startup_config_regenerate = Sys_BackupStartupConfig (path);
		startup_config_readonly = !startup_config_regenerate;
		Com_Printf ("Startup recovery: using default.cfg.\n");
		has_config = false;
	}
	else if (!has_config)
	{
		startup_config_regenerate = GetFileAttributes (path) == INVALID_FILE_ATTRIBUTES;
		startup_config_readonly = !startup_config_regenerate;
	}

	SetFilePointer (startup_config_lock, 0, NULL, FILE_BEGIN);
	if (has_config)
		WriteFile (startup_config_lock, &current, sizeof(current), &count, NULL);
	SetEndOfFile (startup_config_lock);
	FlushFileBuffers (startup_config_lock);
	return has_config;
}

qboolean Sys_StartupConfigRecovering (void)
{
	return startup_config_recovering;
}

qboolean Sys_StartupConfigCanWrite (void)
{
	return startup_config_lock == INVALID_HANDLE_VALUE && !startup_config_readonly;
}

qboolean Sys_StartupConfigDone (void)
{
	qboolean regenerate = startup_config_regenerate;
	startup_config_regenerate = false;
	if (startup_config_lock != INVALID_HANDLE_VALUE)
	{
		SetFilePointer (startup_config_lock, 0, NULL, FILE_BEGIN);
		SetEndOfFile (startup_config_lock);
		CloseHandle (startup_config_lock);
		startup_config_lock = INVALID_HANDLE_VALUE;
		DeleteFile (startup_config_marker);
	}
	return regenerate;
}

void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	CL_Shutdown ();
	Qcommon_Shutdown ();

	va_start (argptr, error);
	vsprintf (text, error, argptr);
	va_end (argptr);

	MessageBox(NULL, text, "Error", 0 /* MB_OK */ );

	if (qwclsemaphore)
		CloseHandle (qwclsemaphore);

// shut down QHOST hooks if necessary
	DeinitConProc ();

	exit (1);
}

void Sys_Quit (void)
{
	timeEndPeriod( 1 );

	Sys_SweepStrayWindows ();		/* BDGN 3.46: no empty task bar buttons after quitting */

	CL_Shutdown();
	Sys_StartupConfigDone ();
	Qcommon_Shutdown ();
	CloseHandle (qwclsemaphore);
	if (dedicated && dedicated->value)
		FreeConsole ();

// shut down QHOST hooks if necessary
	DeinitConProc ();

	exit (0);
}


void WinError (void)
{
	LPVOID lpMsgBuf;

	FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		(LPTSTR) &lpMsgBuf,
		0,
		NULL 
	);

	// Display the string.
	MessageBox( NULL, lpMsgBuf, "GetLastError", MB_OK|MB_ICONINFORMATION );

	// Free the buffer.
	LocalFree( lpMsgBuf );
}

//================================================================


/*
================
Sys_ScanForCD

================
*/
char *Sys_ScanForCD (void)
{
	static char	cddir[MAX_OSPATH];
	static qboolean	done;
#ifndef DEMO
	char		drive[4];
	FILE		*f;
	char		test[MAX_QPATH];

	if (done)		// don't re-check
		return cddir;

	// no abort/retry/fail errors
	SetErrorMode (SEM_FAILCRITICALERRORS);

	drive[0] = 'c';
	drive[1] = ':';
	drive[2] = '\\';
	drive[3] = 0;

	done = true;

	// scan the drives
	for (drive[0] = 'c' ; drive[0] <= 'z' ; drive[0]++)
	{
		// where activision put the stuff...
		sprintf (cddir, "%sinstall\\data", drive);
		sprintf (test, "%sinstall\\data\\quake2.exe", drive);
		f = fopen(test, "r");
		if (f)
		{
			fclose (f);
			if (GetDriveType (drive) == DRIVE_CDROM)
				return cddir;
		}
	}
#endif

	cddir[0] = 0;
	
	return NULL;
}

/*
================
Sys_CopyProtect

================
*/
void	Sys_CopyProtect (void)
{
#ifndef DEMO
	char	*cddir;

	cddir = Sys_ScanForCD();
	if (!cddir[0])
		Com_Error (ERR_FATAL, "You must have the Quake2 CD in the drive to play.");
#endif
}


//================================================================


/*
================
Sys_Init
================
*/
void Sys_Init (void)
{
	OSVERSIONINFO	vinfo;

#if 0
	// allocate a named semaphore on the client so the
	// front end can tell if it is alive

	// mutex will fail if semephore already exists
    qwclsemaphore = CreateMutex(
        NULL,         /* Security attributes */
        0,            /* owner       */
        "qwcl"); /* Semaphore name      */
	if (!qwclsemaphore)
		Sys_Error ("QWCL is already running on this system");
	CloseHandle (qwclsemaphore);

    qwclsemaphore = CreateSemaphore(
        NULL,         /* Security attributes */
        0,            /* Initial count       */
        1,            /* Maximum count       */
        "qwcl"); /* Semaphore name      */
#endif

	timeBeginPeriod( 1 );

	vinfo.dwOSVersionInfoSize = sizeof(vinfo);

	if (!GetVersionEx (&vinfo))
		Sys_Error ("Couldn't get OS info");

	if (vinfo.dwMajorVersion < 4)
		Sys_Error ("Quake2 requires windows version 4 or greater");
	if (vinfo.dwPlatformId == VER_PLATFORM_WIN32s)
		Sys_Error ("Quake2 doesn't run on Win32s");
	else if ( vinfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS )
		s_win95 = true;

	if (dedicated->value)
	{
		if (!AllocConsole ())
			Sys_Error ("Couldn't create dedicated server console");
		hinput = GetStdHandle (STD_INPUT_HANDLE);
		houtput = GetStdHandle (STD_OUTPUT_HANDLE);
	
		// let QHOST hook in
		InitConProc (argc, argv);
	}
}


static char	console_text[256];
static int	console_textlen;

/*
================
Sys_ConsoleInput
================
*/
char *Sys_ConsoleInput (void)
{
	INPUT_RECORD	recs[1024];
	int		dummy;
	int		ch, numread, numevents;

	if (!dedicated || !dedicated->value)
		return NULL;


	for ( ;; )
	{
		if (!GetNumberOfConsoleInputEvents (hinput, &numevents))
			Sys_Error ("Error getting # of console events");

		if (numevents <= 0)
			break;

		if (!ReadConsoleInput(hinput, recs, 1, &numread))
			Sys_Error ("Error reading console input");

		if (numread != 1)
			Sys_Error ("Couldn't read console input");

		if (recs[0].EventType == KEY_EVENT)
		{
			if (!recs[0].Event.KeyEvent.bKeyDown)
			{
				ch = recs[0].Event.KeyEvent.uChar.AsciiChar;

				switch (ch)
				{
					case '\r':
						WriteFile(houtput, "\r\n", 2, &dummy, NULL);	

						if (console_textlen)
						{
							console_text[console_textlen] = 0;
							console_textlen = 0;
							return console_text;
						}
						break;

					case '\b':
						if (console_textlen)
						{
							console_textlen--;
							WriteFile(houtput, "\b \b", 3, &dummy, NULL);	
						}
						break;

					default:
						if (ch >= ' ')
						{
							if (console_textlen < sizeof(console_text)-2)
							{
								WriteFile(houtput, &ch, 1, &dummy, NULL);	
								console_text[console_textlen] = ch;
								console_textlen++;
							}
						}

						break;

				}
			}
		}
	}

	return NULL;
}


/*
================
Sys_ConsoleOutput

Print text to the dedicated console
================
*/
void Sys_ConsoleOutput (char *string)
{
	int		dummy;
	char	text[256];

	if (!dedicated || !dedicated->value)
		return;

	if (console_textlen)
	{
		text[0] = '\r';
		memset(&text[1], ' ', console_textlen);
		text[console_textlen+1] = '\r';
		text[console_textlen+2] = 0;
		WriteFile(houtput, text, console_textlen+2, &dummy, NULL);
	}

	WriteFile(houtput, string, strlen(string), &dummy, NULL);

	if (console_textlen)
		WriteFile(houtput, console_text, console_textlen, &dummy, NULL);
}


/*
================
Sys_SendKeyEvents

Send Key_Event calls
================
*/
void Sys_SendKeyEvents (void)
{
    MSG        msg;

	while (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
	{
		if (!GetMessage (&msg, NULL, 0, 0))
			Sys_Quit ();
		sys_msg_time = msg.time;
      	TranslateMessage (&msg);
      	DispatchMessage (&msg);
	}

	// grab frame time 
	sys_frame_time = timeGetTime();	// FIXME: should this be at start?
}



/*
================
Sys_GetClipboardData

================
*/
char *Sys_GetClipboardData( void )
{
	char *data = NULL;
	char *cliptext;

	if ( OpenClipboard( NULL ) != 0 )
	{
		HANDLE hClipboardData;

		if ( ( hClipboardData = GetClipboardData( CF_TEXT ) ) != 0 )
		{
			if ( ( cliptext = GlobalLock( hClipboardData ) ) != 0 ) 
			{
				data = malloc( GlobalSize( hClipboardData ) + 1 );
				strcpy( data, cliptext );
				GlobalUnlock( hClipboardData );
			}
		}
		CloseClipboard();
	}
	return data;
}

/*
==============================================================================

 WINDOWS CRAP

==============================================================================
*/

/*
=================
Sys_AppActivate
=================
*/
/*
=================
Sys_SweepStrayWindows

BDGN 3.46 (3.49: only reports unless bdgn_sweep is 1): Windows 9x shows an empty grey button on the task bar for every visible top level window without a
title that belongs to this process (and the button stays after the game has quit).  Whatever creates them (the
CD audio / MCI code, a driver, ...), they are found here and hidden, and the window class is printed so the
source can be identified.  Called a few times after a map has loaded and when the game quits.
=================
*/
static int sweep_count;

static BOOL CALLBACK Sys_SweepProc (HWND hwnd, LPARAM lParam)
{
	DWORD	pid = 0;
	char	cls[64], title[8];

	GetWindowThreadProcessId (hwnd, &pid);
	if (pid != GetCurrentProcessId () || hwnd == cl_hwnd)
		return TRUE;
	if (!IsWindowVisible (hwnd))
		return TRUE;
	title[0] = 0;
	GetWindowText (hwnd, title, sizeof(title));
	if (title[0])
		return TRUE;			/* has a caption: a real window (message box, our own window) */
	cls[0] = 0;
	GetClassName (hwnd, cls, sizeof(cls));
	if (!strcmp (cls, "Quake 2"))
		return TRUE;
	{	/* BDGN 3.49: by default only report (once per window); bdgn_sweep 1 also hides it */
		static HWND seen[16];
		static int	nseen;
		static cvar_t *sweep_cv;
		int	i;

		if (!sweep_cv)
			sweep_cv = Cvar_Get ("bdgn_sweep", "0", 0);
		for (i = 0 ; i < nseen ; i++)
			if (seen[i] == hwnd)
				break;
		if (i == nseen && nseen < 16)
		{
			seen[nseen++] = hwnd;
			Com_Printf ("BDGN: stray window (class \"%s\", handle %p)%s\n", cls, (void *)hwnd,
				sweep_cv->value ? ", hidden" : "");
		}
		if (sweep_cv->value)
		{
			ShowWindow (hwnd, SW_HIDE);
			sweep_count++;
		}
	}
	return TRUE;
}

int Sys_SweepStrayWindows (void)
{
	sweep_count = 0;
	EnumWindows (Sys_SweepProc, 0);
	return sweep_count;
}

void Sys_AppActivate (void)
{
	ShowWindow ( cl_hwnd, SW_RESTORE);
	SetForegroundWindow ( cl_hwnd );
}

/*
========================================================================

GAME DLL

========================================================================
*/

static HINSTANCE	game_library;

/*
=================
Sys_UnloadGame
=================
*/
void Sys_UnloadGame (void)
{
	if (!FreeLibrary (game_library))
		Com_Error (ERR_FATAL, "FreeLibrary failed for game library");
	game_library = NULL;
}

/*
=================
Sys_GetGameAPI

Loads the game dll
=================
*/
void *Sys_GetGameAPI (void *parms)
{
	void	*(*GetGameAPI) (void *);
	char	name[MAX_OSPATH];
	char	*path;
	char	cwd[MAX_OSPATH];
#if defined _M_IX86
	const char *gamename = "gamex86.dll";

#ifdef NDEBUG
	const char *debugdir = "release";
#else
	const char *debugdir = "debug";
#endif

#elif defined _M_ALPHA
	const char *gamename = "gameaxp.dll";

#ifdef NDEBUG
	const char *debugdir = "releaseaxp";
#else
	const char *debugdir = "debugaxp";
#endif

#endif

	if (game_library)
		Com_Error (ERR_FATAL, "Sys_GetGameAPI without Sys_UnloadingGame");

	// check the current debug directory first for development purposes
	_getcwd (cwd, sizeof(cwd));
	Com_sprintf (name, sizeof(name), "%s/%s/%s", cwd, debugdir, gamename);
	game_library = LoadLibrary ( name );
	if (game_library)
	{
		Com_DPrintf ("LoadLibrary (%s)\n", name);
	}
	else
	{
#ifdef DEBUG
		// check the current directory for other development purposes
		Com_sprintf (name, sizeof(name), "%s/%s", cwd, gamename);
		game_library = LoadLibrary ( name );
		if (game_library)
		{
			Com_DPrintf ("LoadLibrary (%s)\n", name);
		}
		else
#endif
		{
			// now run through the search paths
			path = NULL;
			while (1)
			{
				path = FS_NextPath (path);
				if (!path)
					return NULL;		// couldn't find one anywhere
#ifdef BDGN_SSE_BUILD
				/* BDGN 3.60e: Quake2SSE.exe takes gamex86sse.dll (built with /arch:SSE) where there is one in this game
				   folder, else gamex86.dll (mods such as ctf/ have only that) */
				Com_sprintf (name, sizeof(name), "%s/gamex86sse.dll", path);
				game_library = LoadLibrary (name);
				if (game_library)
				{
					Com_DPrintf ("LoadLibrary (%s)\n",name);
					break;
				}
#endif
				Com_sprintf (name, sizeof(name), "%s/%s", path, gamename);
				game_library = LoadLibrary (name);
				if (game_library)
				{
					Com_DPrintf ("LoadLibrary (%s)\n",name);
					break;
				}
			}
		}
	}

	GetGameAPI = (void *)GetProcAddress (game_library, "GetGameAPI");
	if (!GetGameAPI)
	{
		Sys_UnloadGame ();		
		return NULL;
	}

	return GetGameAPI (parms);
}

//=======================================================================


/*
==================
ParseCommandLine

==================
*/
void ParseCommandLine (LPSTR lpCmdLine)
{
	argc = 1;
	argv[0] = "exe";

	while (*lpCmdLine && (argc < MAX_NUM_ARGVS))
	{
		while (*lpCmdLine && ((*lpCmdLine <= 32) || (*lpCmdLine > 126)))
			lpCmdLine++;

		if (*lpCmdLine)
		{
			argv[argc] = lpCmdLine;
			argc++;

			while (*lpCmdLine && ((*lpCmdLine > 32) && (*lpCmdLine <= 126)))
				lpCmdLine++;

			if (*lpCmdLine)
			{
				*lpCmdLine = 0;
				lpCmdLine++;
			}
			
		}
	}

}

/*
==================
BDGN 3.52q: crash log. When the game crashes, bdgn_crash.txt (next to quake2.exe) gets the exception, the module
(quake2.exe, ref_soft.dll, ...) and the offset of the crashing instruction in it, the registers, the return
addresses found on the stack (each with its module + offset), the renderer and the main settings. Then Windows
shows its usual message. The offsets can be looked up in the linker map files (obj\<project>\Release\*.map).
==================
*/
static void Crash_Where (FILE *f, const char *what, DWORD addr)
{
	MEMORY_BASIC_INFORMATION mbi;
	char	name[MAX_PATH], *slash;
	name[0] = 0;
	if ( VirtualQuery( (void *)addr, &mbi, sizeof( mbi ) ) && mbi.AllocationBase
		&& GetModuleFileName( (HMODULE)mbi.AllocationBase, name, sizeof( name ) ) )
	{
		slash = strrchr( name, '\\' );
		fprintf( f, "%s %08lx = %s + %lx\n", what, (unsigned long)addr, slash ? slash + 1 : name, (unsigned long)( addr - (DWORD)mbi.AllocationBase ) );
	}
	else
		fprintf( f, "%s %08lx\n", what, (unsigned long)addr );
}
static LONG WINAPI Crash_Filter (EXCEPTION_POINTERS *ep)
{
	FILE	*f = fopen( "bdgn_crash.txt", "a" );
	CONTEXT	*c = ep->ContextRecord;
	DWORD	*sp, i, n = 0;
	MEMORY_BASIC_INFORMATION mbi;
	if ( !f )
		return EXCEPTION_CONTINUE_SEARCH;
	fprintf( f, "==== BDGN %s crash, exception %08lx", BDGN_VERSION_STRING, (unsigned long)ep->ExceptionRecord->ExceptionCode );
	if ( ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2 )
		fprintf( f, " (%s %08lx)", ep->ExceptionRecord->ExceptionInformation[0] ? "write to" : "read from", (unsigned long)ep->ExceptionRecord->ExceptionInformation[1] );
	fprintf( f, "\n" );
	Crash_Where( f, "at", (DWORD)ep->ExceptionRecord->ExceptionAddress );
	fprintf( f, "eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx\n",
		c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp );
	/* return addresses: stack words that point into code of a loaded module */
	sp = (DWORD *)c->Esp;
	for ( i = 0; i < 1024 && n < 16; i++ )
	{
		DWORD v;
		if ( !VirtualQuery( sp + i, &mbi, sizeof( mbi ) ) || mbi.State != MEM_COMMIT || ( mbi.Protect & ( PAGE_NOACCESS | PAGE_GUARD ) ) )
			break;
		v = sp[i];
		if ( VirtualQuery( (void *)v, &mbi, sizeof( mbi ) ) && mbi.Type == MEM_IMAGE
			&& ( mbi.Protect & ( PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY ) ) )
		{
			Crash_Where( f, "  called from", v );
			n++;
		}
	}
	fflush( f );	/* what follows reads cvars: if that fails too, the part above is on disk */
	fprintf( f, "vid_ref %s, gl_driver %s, sw_texfilter %s, sw_coloredlight %s, sw_smp %s, cl_classic2d %s, map %s\n\n",
		Cvar_VariableString( "vid_ref" ), Cvar_VariableString( "gl_driver" ), Cvar_VariableString( "sw_texfilter" ),
		Cvar_VariableString( "sw_coloredlight" ), Cvar_VariableString( "sw_smp" ), Cvar_VariableString( "cl_classic2d" ),
		Cvar_VariableString( "mapname" ) );
	fclose( f );
	return EXCEPTION_CONTINUE_SEARCH;
}

/*
==================
WinMain

==================
*/
HINSTANCE	global_hInstance;

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MSG				msg;
	int				time, oldtime, newtime;
	char			*cddir;

    /* previous instances do not exist in Win32 */
    if (hPrevInstance)
        return 0;

	global_hInstance = hInstance;
	SetUnhandledExceptionFilter( Crash_Filter );	/* BDGN 3.52q: bdgn_crash.txt */
	Com_StartupMark ("WinMain");		// BDGN 3.50: startup timing starts here

	ParseCommandLine (lpCmdLine);

	// if we find the CD, add a +set cddir xxx command line
	cddir = Sys_ScanForCD ();
	if (cddir && argc < MAX_NUM_ARGVS - 3)
	{
		int		i;

		// don't override a cddir on the command line
		for (i=0 ; i<argc ; i++)
			if (!strcmp(argv[i], "cddir"))
				break;
		if (i == argc)
		{
			argv[argc++] = "+set";
			argv[argc++] = "cddir";
			argv[argc++] = cddir;
		}
	}

	Qcommon_Init (argc, argv);
	oldtime = Sys_Milliseconds ();

    /* main window message loop */
	while (1)
	{
		// if at a full screen console, don't update unless needed
		if (Minimized || (dedicated && dedicated->value) )
		{
			Sleep (1);
		}

		while (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
		{
			if (!GetMessage (&msg, NULL, 0, 0))
				Com_Quit ();
			sys_msg_time = msg.time;
			TranslateMessage (&msg);
   			DispatchMessage (&msg);
		}

		do
		{
			newtime = Sys_Milliseconds ();
			time = newtime - oldtime;
		} while (time < 1);
//			Con_Printf ("time:%5.2f - %5.2f = %5.2f\n", newtime, oldtime, time);

		//	_controlfp( ~( _EM_ZERODIVIDE /*| _EM_INVALID*/ ), _MCW_EM );
		_controlfp( _PC_24, _MCW_PC );
		Qcommon_Frame (time);

		oldtime = newtime;
	}

	// never gets here
    return TRUE;
}
