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
// vid.h -- video driver defs

typedef struct vrect_s
{
	int				x,y,width,height;
} vrect_t;

typedef struct
{
	unsigned		width, height;			// coordinates from main game
} viddef_t;

extern	viddef_t	viddef;				// global video state
// BDGN 3.50: menus, HUD and console are laid out on viddef, the real screen divided by vid_scale2d,
// and ref_gl draws them scaled up. 3D view rectangles must be passed through VID_RectToRealPixels.
extern	int			vid_scale2d;
void	VID_RectToRealPixels (int *x, int *y, int *width, int *height);

// Video module initialisation etc
void	VID_Init (void);
void	VID_Shutdown (void);
void	VID_CheckChanges (void);

void	VID_MenuInit( void );
void	VID_MenuDraw( void );
const char *VID_MenuKey( int );
#ifdef _WIN32
void VID_RequestConfigSave (void);
void VID_ApplyLegacyFidelity (void);
qboolean VID_ConfigCanWrite (void);
void VID_ConfigWriteBegin (void);	/* BDGN 3.58r */
void VID_ConfigWriteEnd (void);
qboolean VID_GlideVsyncOn (void);	/* BDGN 3.58m */
qboolean VID_CommitConfigOnFrame (void);
#endif

void VID_GetRealSize (int *width, int *height);
#ifdef _WIN32
void VID_BeginNativeMenu(void);
void VID_EndNativeMenu(void);
#endif
