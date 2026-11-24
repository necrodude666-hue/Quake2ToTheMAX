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

#include "../qcommon/qcommon.h"
#include "winquake.h"
#include "bdgn_winver.h"	/* BDGN 3.51p */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>

//===============================================================================

int		hunkcount;


byte	*membase;
int		hunkmaxsize;
int		cursize;

#define	VIRTUAL_ALLOC

void *Hunk_Begin (int maxsize)
{
	// reserve a huge chunk of memory, but don't commit any yet
	cursize = 0;
	hunkmaxsize = maxsize;
#ifdef VIRTUAL_ALLOC
	membase = VirtualAlloc (NULL, maxsize, MEM_RESERVE, PAGE_NOACCESS);
#else
	membase = malloc (maxsize);
	memset (membase, 0, maxsize);
#endif
	if (!membase)
		Sys_Error ("VirtualAlloc reserve failed");
	return (void *)membase;
}

void *Hunk_Alloc (int size)
{
	void	*buf;

	// Check the aligned size before passing the range to VirtualAlloc.
	if (size<0 || size>0x7fffffff-31)
		Sys_Error ("Hunk_Alloc: invalid allocation size %d", size);
	size = (size+31)&~31;
	if (size>hunkmaxsize-cursize)
		Sys_Error ("Hunk_Alloc: %d bytes requested, %d of %d bytes used", size, cursize, hunkmaxsize);

#ifdef VIRTUAL_ALLOC
	// commit pages as needed
//	buf = VirtualAlloc (membase+cursize, size, MEM_COMMIT, PAGE_READWRITE);
	buf = VirtualAlloc (membase, cursize+size, MEM_COMMIT, PAGE_READWRITE);
	if (!buf)
	{
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &buf, 0, NULL);
		Sys_Error ("VirtualAlloc commit failed.\n%s", buf);
	}
#endif
	cursize += size;

	return (void *)(membase+cursize-size);
}

int Hunk_End (void)
{

	// free the remaining unused virtual memory
#if 0
	void	*buf;

	// write protect it
	buf = VirtualAlloc (membase, cursize, MEM_COMMIT, PAGE_READONLY);
	if (!buf)
		Sys_Error ("VirtualAlloc commit failed");
#endif

	hunkcount++;
//Com_Printf ("hunkcount: %i\n", hunkcount);
	return cursize;
}

void Hunk_Free (void *base)
{
	if ( base )
#ifdef VIRTUAL_ALLOC
		VirtualFree (base, 0, MEM_RELEASE);
#else
		free (base);
#endif

	hunkcount--;
}

//===============================================================================


/*
================
Sys_Milliseconds
================
*/
int	curtime;
/*
================
Sys_ProcessorCounts

BDGN 3.51p: processors Windows reports and processors this process may run on (affinity mask). Windows 95/98/ME
always report 1. GetProcessAffinityMask is looked up at run time, so the exe also loads where it is missing.
================
*/
void Sys_ProcessorCounts (int *system, int *process)
{
	SYSTEM_INFO	info;
	HMODULE		kernel;
	BOOL		(WINAPI *getmask) (HANDLE, PDWORD_PTR, PDWORD_PTR);
	DWORD_PTR	procmask, sysmask;
	int			n;

	memset (&info, 0, sizeof(info));
	GetSystemInfo (&info);
	*system = info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 1;
	*process = *system;
	kernel = GetModuleHandle ("kernel32.dll");
	getmask = kernel ? (BOOL (WINAPI *) (HANDLE, PDWORD_PTR, PDWORD_PTR))GetProcAddress (kernel, "GetProcessAffinityMask") : NULL;
	if (getmask && getmask (GetCurrentProcess (), &procmask, &sysmask))
	{
		for (n = 0 ; procmask ; procmask >>= 1)
			n += (int)(procmask & 1);
		if (n > 0)
			*process = n;
	}
}

/*
================
Sys_OSDescription

BDGN 3.51p: Windows name and version for bdgn_status and the benchmark log (bdgn_winver.h).
================
*/
void Sys_OSDescription (char *out, int size)
{
	OSVERSIONINFO	vi;

	memset (&vi, 0, sizeof(vi));
	vi.dwOSVersionInfoSize = sizeof(vi);
	if (!GetVersionEx (&vi))
	{
		if (size > 0)
		{
			strncpy (out, "unknown Windows", size - 1);
			out[size - 1] = 0;
		}
		return;
	}
	Sys_DescribeWindows (vi.dwPlatformId, vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber, vi.szCSDVersion, out, size);
}

/* BDGN 3.51bdt: physical memory in MB as Windows reports it (a 64 MB machine reports a little less, about 62-63) */
qboolean Sys_HasNTMemoryManager(void)
{
	OSVERSIONINFO vi;
	memset(&vi,0,sizeof(vi));
	vi.dwOSVersionInfoSize=sizeof(vi);
	return GetVersionEx(&vi) && vi.dwPlatformId==VER_PLATFORM_WIN32_NT;
}

int Sys_TotalRamMB (void)
{
	typedef BOOL (WINAPI *memory_status_ex_fn)(LPMEMORYSTATUSEX);
	MEMORYSTATUS ms;
	MEMORYSTATUSEX extended;
	memory_status_ex_fn status_ex;

	/* Resolve at run time so Windows 95/98 can still load the executable. */
	status_ex=(memory_status_ex_fn)GetProcAddress(GetModuleHandleA("kernel32.dll"),"GlobalMemoryStatusEx");
	if (status_ex)
	{
		memset(&extended,0,sizeof(extended));
		extended.dwLength=sizeof(extended);
		if (status_ex(&extended))
			return (int)(extended.ullTotalPhys>>20);
	}
	memset (&ms, 0, sizeof (ms));
	ms.dwLength = sizeof (ms);
	GlobalMemoryStatus (&ms);
	return (int)( ms.dwTotalPhys >> 20 );
}

/* BDGN 3.59i: free physical memory and free page file in KB (map load phases: did the load swap?). GlobalMemoryStatus
   works on Windows 95 to 11; above 4 GB it is capped, which does not matter for the retro machines it is for. */
void Sys_MemStatusKB (int *avail_phys_kb, int *avail_page_kb)
{
	MEMORYSTATUS ms;
	memset (&ms, 0, sizeof (ms));
	ms.dwLength = sizeof (ms);
	GlobalMemoryStatus (&ms);
	if (avail_phys_kb) *avail_phys_kb = (int)(ms.dwAvailPhys >> 10);
	if (avail_page_kb) *avail_page_kb = (int)(ms.dwAvailPageFile >> 10);
}

/* BDGN 3.59s: video memory of the primary display in KB (0 = unknown), from DirectDraw 2's GetAvailableVidMem. ddraw.dll
   is loaded only here and released at once; no DirectX header is needed (the few COM pieces are declared below), so
   the build does not depend on the DirectX SDK. Windows 95 without DirectX 2 or later, or NT 4 without DirectX 3,
   gives 0. With a 3dfx Voodoo1/2 add-on card this is the 2D card's memory, not the 3dfx card's. */
typedef struct { DWORD caps; } bdgn_ddscaps_t;
typedef struct bdgn_dd_s { void **vtbl; } bdgn_dd_t;
int Sys_VideoRamKB (int *free_kb)
{
	static const GUID iid_dd2 = { 0xB3A6F3E0, 0x2B43, 0x11CF, { 0xA2, 0xDE, 0x00, 0xAA, 0x00, 0xB9, 0x33, 0x56 } };
	typedef HRESULT (WINAPI *ddcreate_t) (GUID *, bdgn_dd_t **, void *);
	typedef HRESULT (WINAPI *qi_t) (bdgn_dd_t *, const GUID *, void **);
	typedef ULONG (WINAPI *release_t) (bdgn_dd_t *);
	typedef HRESULT (WINAPI *vidmem_t) (bdgn_dd_t *, bdgn_ddscaps_t *, DWORD *, DWORD *);
	HMODULE lib;
	ddcreate_t create;
	bdgn_dd_t *dd = NULL, *dd2 = NULL;
	bdgn_ddscaps_t caps;
	DWORD total = 0, freemem = 0;
	if (free_kb) *free_kb = 0;
	lib = LoadLibrary ("ddraw.dll");
	if (!lib)
		return 0;
	create = (ddcreate_t)GetProcAddress (lib, "DirectDrawCreate");
	if (create && create (NULL, &dd, NULL) == 0 && dd)
	{
		if (((qi_t)dd->vtbl[0]) (dd, &iid_dd2, (void **)&dd2) == 0 && dd2)
		{
			caps.caps = 0x00004000;	/* DDSCAPS_VIDEOMEMORY */
			if (((vidmem_t)dd2->vtbl[23]) (dd2, &caps, &total, &freemem) != 0)	/* IDirectDraw2::GetAvailableVidMem */
				total = freemem = 0;
			((release_t)dd2->vtbl[2]) (dd2);
		}
		((release_t)dd->vtbl[2]) (dd);
	}
	/* 3.59u: ddraw.dll stays loaded (unloading it again under Windows 9x is avoided) */
	if (free_kb) *free_kb = (int)(freemem >> 10);
	return (int)(total >> 10);
}

/* BDGN 3.51ar: cl_sleep (Sleep(0) gives the rest of the time slice to other threads) */
void Sys_Sleep (int msec)
{
	Sleep (msec < 0 ? 0 : msec);
}

int Sys_Milliseconds (void)
{
	static int		base;
	static qboolean	initialized = false;

	if (!initialized)
	{	// let base retain 16 bits of effectively random data
		base = timeGetTime() & 0xffff0000;
		initialized = true;
	}
	curtime = timeGetTime() - base;

	return curtime;
}

/* BDGN 3.57z: microseconds for bdgn_frametimes (frame, drawing and SwapBuffers in fractions of a millisecond; the
   owner's i7 showed a floor of exactly 3 ms per frame). QueryPerformanceCounter exists from Windows 95 on; without
   it the milliseconds are used. */
unsigned Sys_Microseconds (void)
{
	static LARGE_INTEGER freq;
	static int state;
	LARGE_INTEGER now;
	if (!state)
		state = QueryPerformanceFrequency (&freq) && freq.QuadPart > 0 ? 1 : 2;
	if (state == 2 || !QueryPerformanceCounter (&now))
		return (unsigned)timeGetTime () * 1000u;
	return (unsigned)(now.QuadPart * 1000000 / freq.QuadPart);
}

void Sys_Mkdir (char *path)
{
	_mkdir (path);
}

//============================================

char	findbase[MAX_OSPATH];
char	findpath[MAX_OSPATH];
int		findhandle;

static qboolean CompareAttributes( unsigned found, unsigned musthave, unsigned canthave )
{
	if ( ( found & _A_RDONLY ) && ( canthave & SFF_RDONLY ) )
		return false;
	if ( ( found & _A_HIDDEN ) && ( canthave & SFF_HIDDEN ) )
		return false;
	if ( ( found & _A_SYSTEM ) && ( canthave & SFF_SYSTEM ) )
		return false;
	if ( ( found & _A_SUBDIR ) && ( canthave & SFF_SUBDIR ) )
		return false;
	if ( ( found & _A_ARCH ) && ( canthave & SFF_ARCH ) )
		return false;

	if ( ( musthave & SFF_RDONLY ) && !( found & _A_RDONLY ) )
		return false;
	if ( ( musthave & SFF_HIDDEN ) && !( found & _A_HIDDEN ) )
		return false;
	if ( ( musthave & SFF_SYSTEM ) && !( found & _A_SYSTEM ) )
		return false;
	if ( ( musthave & SFF_SUBDIR ) && !( found & _A_SUBDIR ) )
		return false;
	if ( ( musthave & SFF_ARCH ) && !( found & _A_ARCH ) )
		return false;

	return true;
}

char *Sys_FindFirst (char *path, unsigned musthave, unsigned canthave )
{
	struct _finddata_t findinfo;

	if (findhandle)
		Sys_Error ("Sys_BeginFind without close");
	findhandle = 0;

	COM_FilePath (path, findbase);
	findhandle = _findfirst (path, &findinfo);
	if (findhandle == -1)
		return NULL;
	if ( !CompareAttributes( findinfo.attrib, musthave, canthave ) )
		return NULL;
	Com_sprintf (findpath, sizeof(findpath), "%s/%s", findbase, findinfo.name);
	return findpath;
}

char *Sys_FindNext ( unsigned musthave, unsigned canthave )
{
	struct _finddata_t findinfo;

	if (findhandle == -1)
		return NULL;
	if (_findnext (findhandle, &findinfo) == -1)
		return NULL;
	if ( !CompareAttributes( findinfo.attrib, musthave, canthave ) )
		return NULL;

	Com_sprintf (findpath, sizeof(findpath), "%s/%s", findbase, findinfo.name);
	return findpath;
}

void Sys_FindClose (void)
{
	if (findhandle != -1)
		_findclose (findhandle);
	findhandle = 0;
}


//============================================

