/* BDGN gcc K6-III test build: minimal start code instead of MinGW-w64's CRT objects.

   Why: MinGW-w64's start objects are built for i686 and contain CMOV (the K6-III has none), and they give every DLL a
   TLS directory, which Windows 98 does not handle in a DLL loaded with LoadLibrary.  Here the program and the DLLs
   use only msvcrt.dll (part of Windows 98) and code compiled for the K6-III.
   - EXE: WinMainCRTStartup -> 53-bit x87 precision (as the Visual C++ CRT), WinMain, exit (msvcrt runs atexit).
   - DLL: DllMain returns TRUE (msvcrt.dll initialises itself).
   - pow/ldexp go to msvcrt.dll (MinGW's own versions in libmingwex are built with CMOV). */
#include <windows.h>
#include <float.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

extern double (__cdecl *_imp__pow) (double, double);
extern double (__cdecl *_imp__ldexp) (double, int);
double pow (double x, double y) { return _imp__pow (x, y); }
double ldexp (double x, int e) { return _imp__ldexp (x, e); }
/* the MinGW headers send vsnprintf here; same behaviour as Visual C++ 2005's vsnprintf (msvcrt _vsnprintf) */
int __cdecl __ms_vsnprintf (char *buf, size_t n, const char *fmt, va_list ap) { return _vsnprintf (buf, n, fmt, ap); }
/* the Windows 98 msvcrt.dll has no _vscprintf; MinGW's stand-in would import GetModuleHandleW.  Length of the text,
   -1 above 8 KB (only used to size buffers) */
int __cdecl _vscprintf (const char *fmt, va_list ap) { static char buf[8192]; return _vsnprintf (buf, sizeof (buf), fmt, ap); }

#ifdef BDGN_GCC_EXE
int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);

void WinMainCRTStartup (void)
{
	char		*cmd = GetCommandLineA ();
	STARTUPINFOA	si;
	int			show;

	if (*cmd == '"')
	{	/* skip the quoted program name */
		cmd++;
		while (*cmd && *cmd != '"')
			cmd++;
		if (*cmd == '"')
			cmd++;
	}
	else
		while (*cmd > ' ')
			cmd++;
	while (*cmd && *cmd <= ' ')
		cmd++;
	si.dwFlags = 0;
	GetStartupInfoA (&si);
	show = ( si.dwFlags & STARTF_USESHOWWINDOW ) ? si.wShowWindow : SW_SHOWDEFAULT;
	_controlfp (_PC_53, _MCW_PC);	/* the Visual C++ CRT starts with 53-bit precision */
	exit (WinMain (GetModuleHandleA (NULL), NULL, cmd, show));
}
#else
BOOL WINAPI DllMain (HINSTANCE h, DWORD reason, LPVOID reserved)
{
	(void)h; (void)reason; (void)reserved;
	return TRUE;
}
#endif
