DirectDraw declarations from the Wine-derived mingw-w64 headers distributed with Zig 0.13.0.
Original LGPL 2.1-or-later notice is retained in ddraw.h; see COPYING.LIB.
_mingw_dxhelper.h is public domain.
BDGN adaptations: local helper include and a guarded __MSABI_LONG literal macro for MSVC.
No DirectDraw implementation or modern Windows API is linked. ddraw.dll remains loaded dynamically.
The local quoted include removes the requirement for a separately installed DirectX SDK/header search path.

BDGN v3.51l: use the standard Windows BOOL type instead of MinGW-only WINBOOL in every callback/interface declaration. Both are 32-bit integers in Win32; method order and structures are unchanged.
