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
#include <ctype.h>
#ifdef _WIN32
#include <io.h>
#endif
#include "client.h"
#include "../client/qmenu.h"
#include "snd_midi.h"
#include "../win32/snd_wavmusic.h"

extern char **FS_ListFiles (char *findname, int *numfiles, unsigned musthave, unsigned canthave);

static int	m_main_cursor;

#define NUM_CURSOR_FRAMES 15

static char *menu_in_sound		= "misc/menu1.wav";
static char *menu_move_sound	= "misc/menu2.wav";
static char *menu_out_sound		= "misc/menu3.wav";

void M_Menu_Main_f (void);
	void M_Menu_Game_f (void);
		void M_Menu_LoadGame_f (void);
		void M_Menu_SaveGame_f (void);
		void M_Menu_PlayerConfig_f (void);
			void M_Menu_DownloadOptions_f (void);	// BDGN 3.37: now the "net options" menu
		void M_Menu_Credits_f( void );
	void M_Menu_Multiplayer_f( void );
		void M_Menu_JoinServer_f (void);
			void M_Menu_AddressBook_f( void );
		void M_Menu_StartServer_f (void);
			void M_Menu_DMOptions_f (void);
	void M_Menu_Video_f (void);
	void M_Menu_Options_f (void);
		void M_Menu_Keys_f (void);
		void M_Menu_Speed_f (void);	// BDGN 3.37
	void M_Menu_Quit_f (void);

	void M_Menu_Credits( void );

qboolean	m_entersound;		// play after drawing a frame, so caching
								// won't disrupt the sound

void	(*m_drawfunc) (void);
const char *(*m_keyfunc) (int key);

//=============================================================================
/* Support Routines */

#define	MAX_MENU_DEPTH	8


typedef struct
{
	void	(*draw) (void);
	const char *(*key) (int k);
} menulayer_t;

menulayer_t	m_layers[MAX_MENU_DEPTH];
int		m_menudepth;

static void M_Banner( char *name )
{
	int w, h;

	re.DrawGetPicSize (&w, &h, name );
	re.DrawPic( viddef.width / 2 - w / 2, viddef.height / 2 - 110, name );
}

void M_PushMenu ( void (*draw) (void), const char *(*key) (int k) )
{
	int		i;

	if (Cvar_VariableValue ("maxclients") == 1 
		&& Com_ServerState ())
		Cvar_Set ("paused", "1");

	// if this menu is already present, drop back to that level
	// to avoid stacking menus by hotkeys
	for (i=0 ; i<m_menudepth ; i++)
		if (m_layers[i].draw == draw &&
			m_layers[i].key == key)
		{
			m_menudepth = i;
		}

	if (i == m_menudepth)
	{
		if (m_menudepth >= MAX_MENU_DEPTH)
			Com_Error (ERR_FATAL, "M_PushMenu: MAX_MENU_DEPTH");
		m_layers[m_menudepth].draw = m_drawfunc;
		m_layers[m_menudepth].key = m_keyfunc;
		m_menudepth++;
	}

	m_drawfunc = draw;
	m_keyfunc = key;

	m_entersound = true;

	cls.key_dest = key_menu;
}

void M_ForceMenuOff (void)
{
	/* BDGN 3.58k: save the settings when the menu closes, not only at quit (a crash, a reset or switching the
	   computer off then no longer loses them). CL_WriteConfiguration skips it during the benchmark, before the
	   client is up and while a video change waits for its first frame. */
	if (m_drawfunc)
		CL_WriteConfiguration ();
	m_drawfunc = 0;
	m_keyfunc = 0;
	cls.key_dest = key_game;
	m_menudepth = 0;
	Key_ClearStates ();
	Cvar_Set ("paused", "0");
}

void M_PopMenu (void)
{
	S_StartLocalSound( menu_out_sound );
	if (m_menudepth < 1)
		Com_Error (ERR_FATAL, "M_PopMenu: depth < 1");
	m_menudepth--;

	m_drawfunc = m_layers[m_menudepth].draw;
	m_keyfunc = m_layers[m_menudepth].key;

	if (!m_menudepth)
		M_ForceMenuOff ();
}


const char *Default_MenuKey( menuframework_s *m, int key )
{
	const char *sound = NULL;
	menucommon_s *item;

	if ( m )
	{
		if ( ( item = Menu_ItemAtCursor( m ) ) != 0 )
		{
			if ( item->type == MTYPE_FIELD )
			{
				if ( Field_Key( ( menufield_s * ) item, key ) )
					return NULL;
			}
		}
	}

	switch ( key )
	{
	case K_ESCAPE:
		M_PopMenu();
		return menu_out_sound;
	case K_KP_UPARROW:
	case K_UPARROW:
		if ( m )
		{
			m->cursor--;
			Menu_AdjustCursor( m, -1 );
			sound = menu_move_sound;
		}
		break;
	case K_TAB:
		if ( m )
		{
			m->cursor++;
			Menu_AdjustCursor( m, 1 );
			sound = menu_move_sound;
		}
		break;
	case K_KP_DOWNARROW:
	case K_DOWNARROW:
		if ( m )
		{
			m->cursor++;
			Menu_AdjustCursor( m, 1 );
			sound = menu_move_sound;
		}
		break;
	case K_KP_LEFTARROW:
	case K_LEFTARROW:
		if ( m )
		{
			Menu_SlideItem( m, -1 );
			sound = menu_move_sound;
		}
		break;
	case K_KP_RIGHTARROW:
	case K_RIGHTARROW:
		if ( m )
		{
			Menu_SlideItem( m, 1 );
			sound = menu_move_sound;
		}
		break;

	case K_MOUSE1:
	case K_MOUSE2:
	case K_MOUSE3:
	case K_JOY1:
	case K_JOY2:
	case K_JOY3:
	case K_JOY4:
	case K_AUX1:
	case K_AUX2:
	case K_AUX3:
	case K_AUX4:
	case K_AUX5:
	case K_AUX6:
	case K_AUX7:
	case K_AUX8:
	case K_AUX9:
	case K_AUX10:
	case K_AUX11:
	case K_AUX12:
	case K_AUX13:
	case K_AUX14:
	case K_AUX15:
	case K_AUX16:
	case K_AUX17:
	case K_AUX18:
	case K_AUX19:
	case K_AUX20:
	case K_AUX21:
	case K_AUX22:
	case K_AUX23:
	case K_AUX24:
	case K_AUX25:
	case K_AUX26:
	case K_AUX27:
	case K_AUX28:
	case K_AUX29:
	case K_AUX30:
	case K_AUX31:
	case K_AUX32:
		
	case K_KP_ENTER:
	case K_ENTER:
		if ( m )
			Menu_SelectItem( m );
		sound = menu_move_sound;
		break;
	}

	return sound;
}

//=============================================================================

/*
================
M_DrawCharacter

Draws one solid graphics character
cx and cy are in 320*240 coordinates, and will be centered on
higher res screens.
================
*/
void M_DrawCharacter (int cx, int cy, int num)
{
	re.DrawChar ( cx + ((viddef.width - 320)>>1), cy + ((viddef.height - 240)>>1), num);
}

void M_Print (int cx, int cy, char *str)
{
	while (*str)
	{
		M_DrawCharacter (cx, cy, (*str)+128);
		str++;
		cx += 8;
	}
}

void M_PrintWhite (int cx, int cy, char *str)
{
	while (*str)
	{
		M_DrawCharacter (cx, cy, *str);
		str++;
		cx += 8;
	}
}

void M_DrawPic (int x, int y, char *pic)
{
	re.DrawPic (x + ((viddef.width - 320)>>1), y + ((viddef.height - 240)>>1), pic);
}


/*
=============
M_DrawCursor

Draws an animating cursor with the point at
x,y.  The pic will extend to the left of x,
and both above and below y.
=============
*/
void M_DrawCursor( int x, int y, int f )
{
	char	cursorname[80];
	static qboolean cached;

	if ( !cached )
	{
		int i;

		for ( i = 0; i < NUM_CURSOR_FRAMES; i++ )
		{
			Com_sprintf( cursorname, sizeof( cursorname ), "m_cursor%d", i );

			re.RegisterPic( cursorname );
		}
		cached = true;
	}

	Com_sprintf( cursorname, sizeof(cursorname), "m_cursor%d", f );
	re.DrawPic( x, y, cursorname );
}

void M_DrawTextBox (int x, int y, int width, int lines)
{
	int		cx, cy;
	int		n;

	// draw left side
	cx = x;
	cy = y;
	M_DrawCharacter (cx, cy, 1);
	for (n = 0; n < lines; n++)
	{
		cy += 8;
		M_DrawCharacter (cx, cy, 4);
	}
	M_DrawCharacter (cx, cy+8, 7);

	// draw middle
	cx += 8;
	while (width > 0)
	{
		cy = y;
		M_DrawCharacter (cx, cy, 2);
		for (n = 0; n < lines; n++)
		{
			cy += 8;
			M_DrawCharacter (cx, cy, 5);
		}
		M_DrawCharacter (cx, cy+8, 8);
		width -= 1;
		cx += 8;
	}

	// draw right side
	cy = y;
	M_DrawCharacter (cx, cy, 3);
	for (n = 0; n < lines; n++)
	{
		cy += 8;
		M_DrawCharacter (cx, cy, 6);
	}
	M_DrawCharacter (cx, cy+8, 9);
}

		
/*
=======================================================================

MAIN MENU

=======================================================================
*/
#define	MAIN_ITEMS	5


void M_Main_Draw (void)
{
	int i;
	int w, h;
	int ystart;
	int	xoffset;
	int widest = -1;
	int totalheight = 0;
	char litname[80];
	char *names[] =
	{
		"m_main_game",
		"m_main_multiplayer",
		"m_main_options",
		"m_main_video",
		"m_main_quit",
		0
	};

	for ( i = 0; names[i] != 0; i++ )
	{
		re.DrawGetPicSize( &w, &h, names[i] );

		if ( w > widest )
			widest = w;
		totalheight += ( h + 12 );
	}

	ystart = ( viddef.height / 2 - 110 );
	xoffset = ( viddef.width - widest + 70 ) / 2;

	for ( i = 0; names[i] != 0; i++ )
	{
		if ( i != m_main_cursor )
			re.DrawPic( xoffset, ystart + i * 40 + 13, names[i] );
	}
	strcpy( litname, names[m_main_cursor] );
	strcat( litname, "_sel" );
	re.DrawPic( xoffset, ystart + m_main_cursor * 40 + 13, litname );

	M_DrawCursor( xoffset - 25, ystart + m_main_cursor * 40 + 11, (int)(cls.realtime / 100)%NUM_CURSOR_FRAMES );

	re.DrawGetPicSize( &w, &h, "m_main_plaque" );
	re.DrawPic( xoffset - 30 - w, ystart, "m_main_plaque" );

	re.DrawPic( xoffset - 30 - w, ystart + h + 5, "m_main_logo" );
}


const char *M_Main_Key (int key)
{
	const char *sound = menu_move_sound;

	switch (key)
	{
	case K_ESCAPE:
		M_PopMenu ();
		break;

	case K_KP_DOWNARROW:
	case K_DOWNARROW:
		if (++m_main_cursor >= MAIN_ITEMS)
			m_main_cursor = 0;
		return sound;

	case K_KP_UPARROW:
	case K_UPARROW:
		if (--m_main_cursor < 0)
			m_main_cursor = MAIN_ITEMS - 1;
		return sound;

	case K_KP_ENTER:
	case K_ENTER:
		m_entersound = true;

		switch (m_main_cursor)
		{
		case 0:
			M_Menu_Game_f ();
			break;

		case 1:
			M_Menu_Multiplayer_f();
			break;

		case 2:
			M_Menu_Options_f ();
			break;

		case 3:
			M_Menu_Video_f ();
			break;

		case 4:
			M_Menu_Quit_f ();
			break;
		}
	}

	return NULL;
}


void M_Menu_Main_f (void)
{
	if (cls.state != ca_active)		// BDGN 3.49h: main menu theme (music mode "retro MIDI")
		MIDI_MenuMusic ();
	M_PushMenu (M_Main_Draw, M_Main_Key);
}

/*
=======================================================================

MULTIPLAYER MENU

=======================================================================
*/
static menuframework_s	s_multiplayer_menu;
static menuaction_s		s_join_network_server_action;
static menuaction_s		s_start_network_server_action;
static menuaction_s		s_player_setup_action;

static void Multiplayer_MenuDraw (void)
{
	M_Banner( "m_banner_multiplayer" );

	Menu_AdjustCursor( &s_multiplayer_menu, 1 );
	Menu_Draw( &s_multiplayer_menu );
}

static void PlayerSetupFunc( void *unused )
{
	M_Menu_PlayerConfig_f();
}

static void JoinNetworkServerFunc( void *unused )
{
	M_Menu_JoinServer_f();
}

static void StartNetworkServerFunc( void *unused )
{
	M_Menu_StartServer_f ();
}

void Multiplayer_MenuInit( void )
{
	s_multiplayer_menu.x = viddef.width * 0.50 - 64;
	s_multiplayer_menu.nitems = 0;

	s_join_network_server_action.generic.type	= MTYPE_ACTION;
	s_join_network_server_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_join_network_server_action.generic.x		= 0;
	s_join_network_server_action.generic.y		= 0;
	s_join_network_server_action.generic.name	= " join network server";
	s_join_network_server_action.generic.callback = JoinNetworkServerFunc;

	s_start_network_server_action.generic.type	= MTYPE_ACTION;
	s_start_network_server_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_start_network_server_action.generic.x		= 0;
	s_start_network_server_action.generic.y		= 10;
	s_start_network_server_action.generic.name	= " start network server";
	s_start_network_server_action.generic.callback = StartNetworkServerFunc;

	s_player_setup_action.generic.type	= MTYPE_ACTION;
	s_player_setup_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_player_setup_action.generic.x		= 0;
	s_player_setup_action.generic.y		= 20;
	s_player_setup_action.generic.name	= " player setup";
	s_player_setup_action.generic.callback = PlayerSetupFunc;

	Menu_AddItem( &s_multiplayer_menu, ( void * ) &s_join_network_server_action );
	Menu_AddItem( &s_multiplayer_menu, ( void * ) &s_start_network_server_action );
	Menu_AddItem( &s_multiplayer_menu, ( void * ) &s_player_setup_action );

	Menu_SetStatusBar( &s_multiplayer_menu, NULL );

	Menu_Center( &s_multiplayer_menu );
}

const char *Multiplayer_MenuKey( int key )
{
	return Default_MenuKey( &s_multiplayer_menu, key );
}

void M_Menu_Multiplayer_f( void )
{
	Multiplayer_MenuInit();
	M_PushMenu( Multiplayer_MenuDraw, Multiplayer_MenuKey );
}

/*
=======================================================================

KEYS MENU

=======================================================================
*/
char *bindnames[][2] =
{
{"+attack", 		"attack"},
{"weapnext", 		"next weapon"},
// add previous weapon
{"weapprev", 		"previous weapon"},
//
{"+forward", 		"walk forward"},
{"+back", 			"backpedal"},
/* legacy shit!
{"+left", 			"turn left"},
{"+right", 			"turn right"},
*/
{"+speed", 			"run"},
{"+moveleft", 		"step left"},
{"+moveright", 		"step right"},
/* legacy shit!
{"+strafe", 		"sidestep"},
{"+lookup", 		"look up"},
{"+lookdown", 		"look down"},
{"centerview", 		"center view"},
{"+mlook", 			"mouse look"},
{"+klook", 			"keyboard look"},
*/
{"+moveup",			"up / jump"},
{"+movedown",		"down / crouch"},

{"inven",			"inventory"},
{"invuse",			"use item"},
{"invdrop",			"drop item"},
{"invprev",			"prev item"},
{"invnext",			"next item"},

// weapons
{"use Blaster",				"blaster"},
{"use Shotgun", 			"shotgun"},
{"use Super Shotgun",	 	"super shotgun"},
{"use Machinegun",			"machinegun"},
{"use Chaingun",			"chaingun"},
{"use Grenade Launcher", 	"grenade launcher"},
{"use Rocket Launcher", 	"rocket launcher"},
{"use HyperBlaster",		"hyperblaster"},
{"use Railgun", 			"railgun"},
{"use BFG10K", 				"bfg10k"},
{"use grenades", 			"grenade"},
// end
{"+zoom", 			"zoom"},
//zoom
// end

{"cmd help", 		"help computer" }, 
{ 0, 0 }
};

int				keys_cursor;
static int		bind_grab;

static menuframework_s	s_keys_menu;
static menuaction_s		s_keys_attack_action;
static menuaction_s		s_keys_change_weapon_action;
static menuaction_s		s_keys_change_weapon_action_previous;
static menuaction_s		s_keys_walk_forward_action;
static menuaction_s		s_keys_backpedal_action;
//legacy shit!
static menuaction_s		s_keys_turn_left_action;
static menuaction_s		s_keys_turn_right_action;
//
static menuaction_s		s_keys_run_action;
static menuaction_s		s_keys_step_left_action;
static menuaction_s		s_keys_step_right_action;
//legacy shit!
//static menuaction_s		s_keys_sidestep_action;
//static menuaction_s		s_keys_look_up_action;
//static menuaction_s		s_keys_look_down_action;
//static menuaction_s		s_keys_center_view_action;
//static menuaction_s		s_keys_mouse_look_action;
//static menuaction_s		s_keys_keyboard_look_action;
static menuaction_s		s_keys_move_up_action;
static menuaction_s		s_keys_move_down_action;
static menuaction_s		s_keys_inventory_action;
static menuaction_s		s_keys_inv_use_action;
static menuaction_s		s_keys_inv_drop_action;
static menuaction_s		s_keys_inv_prev_action;
static menuaction_s		s_keys_inv_next_action;

// weapons
static menuaction_s		s_keys_blaster_action;
static menuaction_s		s_keys_shotgun_action;
static menuaction_s		s_keys_super_shotgun_action;
static menuaction_s		s_keys_machinegun_action;
static menuaction_s		s_keys_chaingun_action;
static menuaction_s		s_keys_grenade_launcher_action;
static menuaction_s		s_keys_rocket_launcher_action;
static menuaction_s		s_keys_hyperblaster_action;
static menuaction_s		s_keys_railgun_action;
static menuaction_s		s_keys_bfg10k_action;
static menuaction_s		s_keys_grenades_action;
// end
static menuaction_s		s_keys_zoom_action;
// zoom

// end

static menuaction_s		s_keys_help_computer_action;

static void M_UnbindCommand (char *command)
{
	int		j;
	int		l;
	char	*b;

	l = strlen(command);

	for (j=0 ; j<256 ; j++)
	{
		b = keybindings[j];
		if (!b)
			continue;
		if (!strncmp (b, command, l) )
			Key_SetBinding (j, "");
	}
}

static void M_FindKeysForCommand (char *command, int *twokeys)
{
	int		count;
	int		j;
	int		l;
	char	*b;

	twokeys[0] = twokeys[1] = -1;
	l = strlen(command);
	count = 0;

	for (j=0 ; j<256 ; j++)
	{
		b = keybindings[j];
		if (!b)
			continue;
		if (!strncmp (b, command, l) )
		{
			twokeys[count] = j;
			count++;
			if (count == 2)
				break;
		}
	}
}

static void KeyCursorDrawFunc( menuframework_s *menu )
{
/*
	if ( bind_grab )
		re.DrawChar( menu->x, menu->y + menu->cursor * 9, '=' );
	else
		re.DrawChar( menu->x, menu->y + menu->cursor * 9, 12 + ( ( int ) ( Sys_Milliseconds() / 250 ) & 1 ) );
*/
	// compress the menu vertically one pixel per entry
	if ( bind_grab )
		re.DrawChar( menu->x, menu->y + menu->cursor * 8, '=' );
	else
		re.DrawChar( menu->x, menu->y + menu->cursor * 8, 12 + ( ( int ) ( Sys_Milliseconds() / 250 ) & 1 ) );
}

static void DrawKeyBindingFunc( void *self )
{
	int keys[2];
	menuaction_s *a = ( menuaction_s * ) self;

	M_FindKeysForCommand( bindnames[a->generic.localdata[0]][0], keys);
		
	if (keys[0] == -1)
	{
		Menu_DrawString( a->generic.x + a->generic.parent->x + 16, a->generic.y + a->generic.parent->y, "???" );
	}
	else
	{
		int x;
		const char *name;

		name = Key_KeynumToString (keys[0]);

		Menu_DrawString( a->generic.x + a->generic.parent->x + 16, a->generic.y + a->generic.parent->y, name );

		x = strlen(name) * 8;

		if (keys[1] != -1)
		{
			Menu_DrawString( a->generic.x + a->generic.parent->x + 24 + x, a->generic.y + a->generic.parent->y, "or" );
			Menu_DrawString( a->generic.x + a->generic.parent->x + 48 + x, a->generic.y + a->generic.parent->y, Key_KeynumToString (keys[1]) );
		}
	}
}

static void KeyBindingFunc( void *self )
{
	menuaction_s *a = ( menuaction_s * ) self;
	int keys[2];

	M_FindKeysForCommand( bindnames[a->generic.localdata[0]][0], keys );

	if (keys[1] != -1)
		M_UnbindCommand( bindnames[a->generic.localdata[0]][0]);

	bind_grab = true;

	Menu_SetStatusBar( &s_keys_menu, "press a key or button for this action" );
}

static void Keys_MenuInit( void )
{
	int y = 0;
	int i = 0;

	s_keys_menu.x = viddef.width * 0.50;
	s_keys_menu.nitems = 0;
	s_keys_menu.cursordraw = KeyCursorDrawFunc;

	s_keys_attack_action.generic.type	= MTYPE_ACTION;
	s_keys_attack_action.generic.flags  = QMF_GRAYED;
	s_keys_attack_action.generic.x		= 0;
	s_keys_attack_action.generic.y		= y;
	s_keys_attack_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_attack_action.generic.localdata[0] = i;
	s_keys_attack_action.generic.name	= bindnames[s_keys_attack_action.generic.localdata[0]][1];

	s_keys_change_weapon_action.generic.type	= MTYPE_ACTION;
	s_keys_change_weapon_action.generic.flags  = QMF_GRAYED;
	s_keys_change_weapon_action.generic.x		= 0;
//	s_keys_change_weapon_action.generic.y		= y += 9;
	s_keys_change_weapon_action.generic.y		= y += 8;
	s_keys_change_weapon_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_change_weapon_action.generic.localdata[0] = ++i;
	s_keys_change_weapon_action.generic.name	= bindnames[s_keys_change_weapon_action.generic.localdata[0]][1];
	
	// previous weapon
	s_keys_change_weapon_action_previous.generic.type	= MTYPE_ACTION;
	s_keys_change_weapon_action_previous.generic.flags  = QMF_GRAYED;
	s_keys_change_weapon_action_previous.generic.x		= 0;
//	s_keys_change_weapon_action_previous.generic.y		= y += 9;
	s_keys_change_weapon_action_previous.generic.y		= y += 8;
	s_keys_change_weapon_action_previous.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_change_weapon_action_previous.generic.localdata[0] = ++i;
	s_keys_change_weapon_action_previous.generic.name	= bindnames[s_keys_change_weapon_action_previous.generic.localdata[0]][1];
	//

	s_keys_walk_forward_action.generic.type	= MTYPE_ACTION;
	s_keys_walk_forward_action.generic.flags  = QMF_GRAYED;
	s_keys_walk_forward_action.generic.x		= 0;
//	s_keys_walk_forward_action.generic.y		= y += 9;
	s_keys_walk_forward_action.generic.y		= y += 8;
	s_keys_walk_forward_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_walk_forward_action.generic.localdata[0] = ++i;
	s_keys_walk_forward_action.generic.name	= bindnames[s_keys_walk_forward_action.generic.localdata[0]][1];

	s_keys_backpedal_action.generic.type	= MTYPE_ACTION;
	s_keys_backpedal_action.generic.flags  = QMF_GRAYED;
	s_keys_backpedal_action.generic.x		= 0;
//	s_keys_backpedal_action.generic.y		= y += 9;
	s_keys_backpedal_action.generic.y		= y += 8;
	s_keys_backpedal_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_backpedal_action.generic.localdata[0] = ++i;
	s_keys_backpedal_action.generic.name	= bindnames[s_keys_backpedal_action.generic.localdata[0]][1];

/* legacy shit!
	s_keys_turn_left_action.generic.type	= MTYPE_ACTION;
	s_keys_turn_left_action.generic.flags  = QMF_GRAYED;
	s_keys_turn_left_action.generic.x		= 0;
	s_keys_turn_left_action.generic.y		= y += 9;
	s_keys_turn_left_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_turn_left_action.generic.localdata[0] = ++i;
	s_keys_turn_left_action.generic.name	= bindnames[s_keys_turn_left_action.generic.localdata[0]][1];

	s_keys_turn_right_action.generic.type	= MTYPE_ACTION;
	s_keys_turn_right_action.generic.flags  = QMF_GRAYED;
	s_keys_turn_right_action.generic.x		= 0;
	s_keys_turn_right_action.generic.y		= y += 9;
	s_keys_turn_right_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_turn_right_action.generic.localdata[0] = ++i;
	s_keys_turn_right_action.generic.name	= bindnames[s_keys_turn_right_action.generic.localdata[0]][1];
*/

	s_keys_run_action.generic.type	= MTYPE_ACTION;
	s_keys_run_action.generic.flags  = QMF_GRAYED;
	s_keys_run_action.generic.x		= 0;
//	s_keys_run_action.generic.y		= y += 9;
	s_keys_run_action.generic.y		= y += 8;
	s_keys_run_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_run_action.generic.localdata[0] = ++i;
	s_keys_run_action.generic.name	= bindnames[s_keys_run_action.generic.localdata[0]][1];

	s_keys_step_left_action.generic.type	= MTYPE_ACTION;
	s_keys_step_left_action.generic.flags  = QMF_GRAYED;
	s_keys_step_left_action.generic.x		= 0;
//	s_keys_step_left_action.generic.y		= y += 9;
	s_keys_step_left_action.generic.y		= y += 8;
	s_keys_step_left_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_step_left_action.generic.localdata[0] = ++i;
	s_keys_step_left_action.generic.name	= bindnames[s_keys_step_left_action.generic.localdata[0]][1];

	s_keys_step_right_action.generic.type	= MTYPE_ACTION;
	s_keys_step_right_action.generic.flags  = QMF_GRAYED;
	s_keys_step_right_action.generic.x		= 0;
//	s_keys_step_right_action.generic.y		= y += 9;
	s_keys_step_right_action.generic.y		= y += 8;
	s_keys_step_right_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_step_right_action.generic.localdata[0] = ++i;
	s_keys_step_right_action.generic.name	= bindnames[s_keys_step_right_action.generic.localdata[0]][1];

/* legacy shit!
	s_keys_sidestep_action.generic.type	= MTYPE_ACTION;
	s_keys_sidestep_action.generic.flags  = QMF_GRAYED;
	s_keys_sidestep_action.generic.x		= 0;
	s_keys_sidestep_action.generic.y		= y += 9;
	s_keys_sidestep_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_sidestep_action.generic.localdata[0] = ++i;
	s_keys_sidestep_action.generic.name	= bindnames[s_keys_sidestep_action.generic.localdata[0]][1];

	s_keys_look_up_action.generic.type	= MTYPE_ACTION;
	s_keys_look_up_action.generic.flags  = QMF_GRAYED;
	s_keys_look_up_action.generic.x		= 0;
	s_keys_look_up_action.generic.y		= y += 9;
	s_keys_look_up_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_look_up_action.generic.localdata[0] = ++i;
	s_keys_look_up_action.generic.name	= bindnames[s_keys_look_up_action.generic.localdata[0]][1];

	s_keys_look_down_action.generic.type	= MTYPE_ACTION;
	s_keys_look_down_action.generic.flags  = QMF_GRAYED;
	s_keys_look_down_action.generic.x		= 0;
	s_keys_look_down_action.generic.y		= y += 9;
	s_keys_look_down_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_look_down_action.generic.localdata[0] = ++i;
	s_keys_look_down_action.generic.name	= bindnames[s_keys_look_down_action.generic.localdata[0]][1];

	s_keys_center_view_action.generic.type	= MTYPE_ACTION;
	s_keys_center_view_action.generic.flags  = QMF_GRAYED;
	s_keys_center_view_action.generic.x		= 0;
	s_keys_center_view_action.generic.y		= y += 9;
	s_keys_center_view_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_center_view_action.generic.localdata[0] = ++i;
	s_keys_center_view_action.generic.name	= bindnames[s_keys_center_view_action.generic.localdata[0]][1];

	s_keys_mouse_look_action.generic.type	= MTYPE_ACTION;
	s_keys_mouse_look_action.generic.flags  = QMF_GRAYED;
	s_keys_mouse_look_action.generic.x		= 0;
	s_keys_mouse_look_action.generic.y		= y += 9;
	s_keys_mouse_look_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_mouse_look_action.generic.localdata[0] = ++i;
	s_keys_mouse_look_action.generic.name	= bindnames[s_keys_mouse_look_action.generic.localdata[0]][1];

	s_keys_keyboard_look_action.generic.type	= MTYPE_ACTION;
	s_keys_keyboard_look_action.generic.flags  = QMF_GRAYED;
	s_keys_keyboard_look_action.generic.x		= 0;
	s_keys_keyboard_look_action.generic.y		= y += 9;
	s_keys_keyboard_look_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_keyboard_look_action.generic.localdata[0] = ++i;
	s_keys_keyboard_look_action.generic.name	= bindnames[s_keys_keyboard_look_action.generic.localdata[0]][1];
*/

	s_keys_move_up_action.generic.type	= MTYPE_ACTION;
	s_keys_move_up_action.generic.flags  = QMF_GRAYED;
	s_keys_move_up_action.generic.x		= 0;
	s_keys_move_up_action.generic.y		= y += 8;
	s_keys_move_up_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_move_up_action.generic.localdata[0] = ++i;
	s_keys_move_up_action.generic.name	= bindnames[s_keys_move_up_action.generic.localdata[0]][1];

	s_keys_move_down_action.generic.type	= MTYPE_ACTION;
	s_keys_move_down_action.generic.flags  = QMF_GRAYED;
	s_keys_move_down_action.generic.x		= 0;
	s_keys_move_down_action.generic.y		= y += 8;
	s_keys_move_down_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_move_down_action.generic.localdata[0] = ++i;
	s_keys_move_down_action.generic.name	= bindnames[s_keys_move_down_action.generic.localdata[0]][1];

	s_keys_inventory_action.generic.type	= MTYPE_ACTION;
	s_keys_inventory_action.generic.flags  = QMF_GRAYED;
	s_keys_inventory_action.generic.x		= 0;
	s_keys_inventory_action.generic.y		= y += 8;
	s_keys_inventory_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_inventory_action.generic.localdata[0] = ++i;
	s_keys_inventory_action.generic.name	= bindnames[s_keys_inventory_action.generic.localdata[0]][1];

	s_keys_inv_use_action.generic.type	= MTYPE_ACTION;
	s_keys_inv_use_action.generic.flags  = QMF_GRAYED;
	s_keys_inv_use_action.generic.x		= 0;
	s_keys_inv_use_action.generic.y		= y += 8;
	s_keys_inv_use_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_inv_use_action.generic.localdata[0] = ++i;
	s_keys_inv_use_action.generic.name	= bindnames[s_keys_inv_use_action.generic.localdata[0]][1];

	s_keys_inv_drop_action.generic.type	= MTYPE_ACTION;
	s_keys_inv_drop_action.generic.flags  = QMF_GRAYED;
	s_keys_inv_drop_action.generic.x		= 0;
	s_keys_inv_drop_action.generic.y		= y += 8;
	s_keys_inv_drop_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_inv_drop_action.generic.localdata[0] = ++i;
	s_keys_inv_drop_action.generic.name	= bindnames[s_keys_inv_drop_action.generic.localdata[0]][1];

	s_keys_inv_prev_action.generic.type	= MTYPE_ACTION;
	s_keys_inv_prev_action.generic.flags  = QMF_GRAYED;
	s_keys_inv_prev_action.generic.x		= 0;
	s_keys_inv_prev_action.generic.y		= y += 8;
	s_keys_inv_prev_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_inv_prev_action.generic.localdata[0] = ++i;
	s_keys_inv_prev_action.generic.name	= bindnames[s_keys_inv_prev_action.generic.localdata[0]][1];

	s_keys_inv_next_action.generic.type	= MTYPE_ACTION;
	s_keys_inv_next_action.generic.flags  = QMF_GRAYED;
	s_keys_inv_next_action.generic.x		= 0;
	s_keys_inv_next_action.generic.y		= y += 8;
	s_keys_inv_next_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_inv_next_action.generic.localdata[0] = ++i;
	s_keys_inv_next_action.generic.name	= bindnames[s_keys_inv_next_action.generic.localdata[0]][1];
	
	// weapons
	s_keys_blaster_action.generic.type	= MTYPE_ACTION;
	s_keys_blaster_action.generic.flags  = QMF_GRAYED;
	s_keys_blaster_action.generic.x		= 0;
	s_keys_blaster_action.generic.y		= y += 8;
	s_keys_blaster_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_blaster_action.generic.localdata[0] = ++i;
	s_keys_blaster_action.generic.name	= bindnames[s_keys_blaster_action.generic.localdata[0]][1];

	s_keys_shotgun_action.generic.type	= MTYPE_ACTION;
	s_keys_shotgun_action.generic.flags  = QMF_GRAYED;
	s_keys_shotgun_action.generic.x		= 0;
	s_keys_shotgun_action.generic.y		= y += 8;
	s_keys_shotgun_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_shotgun_action.generic.localdata[0] = ++i;
	s_keys_shotgun_action.generic.name	= bindnames[s_keys_shotgun_action.generic.localdata[0]][1];
	
	s_keys_super_shotgun_action.generic.type	= MTYPE_ACTION;
	s_keys_super_shotgun_action.generic.flags  = QMF_GRAYED;
	s_keys_super_shotgun_action.generic.x		= 0;
	s_keys_super_shotgun_action.generic.y		= y += 8;
	s_keys_super_shotgun_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_super_shotgun_action.generic.localdata[0] = ++i;
	s_keys_super_shotgun_action.generic.name	= bindnames[s_keys_super_shotgun_action.generic.localdata[0]][1];
	
	s_keys_machinegun_action.generic.type	= MTYPE_ACTION;
	s_keys_machinegun_action.generic.flags  = QMF_GRAYED;
	s_keys_machinegun_action.generic.x		= 0;
	s_keys_machinegun_action.generic.y		= y += 8;
	s_keys_machinegun_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_machinegun_action.generic.localdata[0] = ++i;
	s_keys_machinegun_action.generic.name	= bindnames[s_keys_machinegun_action.generic.localdata[0]][1];
		
	s_keys_chaingun_action.generic.type	= MTYPE_ACTION;
	s_keys_chaingun_action.generic.flags  = QMF_GRAYED;
	s_keys_chaingun_action.generic.x		= 0;
	s_keys_chaingun_action.generic.y		= y += 8;
	s_keys_chaingun_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_chaingun_action.generic.localdata[0] = ++i;
	s_keys_chaingun_action.generic.name	= bindnames[s_keys_chaingun_action.generic.localdata[0]][1];
			
	s_keys_grenade_launcher_action.generic.type	= MTYPE_ACTION;
	s_keys_grenade_launcher_action.generic.flags  = QMF_GRAYED;
	s_keys_grenade_launcher_action.generic.x		= 0;
	s_keys_grenade_launcher_action.generic.y		= y += 8;
	s_keys_grenade_launcher_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_grenade_launcher_action.generic.localdata[0] = ++i;
	s_keys_grenade_launcher_action.generic.name	= bindnames[s_keys_grenade_launcher_action.generic.localdata[0]][1];
				
	s_keys_rocket_launcher_action.generic.type	= MTYPE_ACTION;
	s_keys_rocket_launcher_action.generic.flags  = QMF_GRAYED;
	s_keys_rocket_launcher_action.generic.x		= 0;
	s_keys_rocket_launcher_action.generic.y		= y += 8;
	s_keys_rocket_launcher_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_rocket_launcher_action.generic.localdata[0] = ++i;
	s_keys_rocket_launcher_action.generic.name	= bindnames[s_keys_rocket_launcher_action.generic.localdata[0]][1];

	s_keys_hyperblaster_action.generic.type	= MTYPE_ACTION;
	s_keys_hyperblaster_action.generic.flags  = QMF_GRAYED;
	s_keys_hyperblaster_action.generic.x		= 0;
	s_keys_hyperblaster_action.generic.y		= y += 8;
	s_keys_hyperblaster_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_hyperblaster_action.generic.localdata[0] = ++i;
	s_keys_hyperblaster_action.generic.name	= bindnames[s_keys_hyperblaster_action.generic.localdata[0]][1];
	
	s_keys_railgun_action.generic.type	= MTYPE_ACTION;
	s_keys_railgun_action.generic.flags  = QMF_GRAYED;
	s_keys_railgun_action.generic.x		= 0;
	s_keys_railgun_action.generic.y		= y += 8;
	s_keys_railgun_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_railgun_action.generic.localdata[0] = ++i;
	s_keys_railgun_action.generic.name	= bindnames[s_keys_railgun_action.generic.localdata[0]][1];
		
	s_keys_bfg10k_action.generic.type	= MTYPE_ACTION;
	s_keys_bfg10k_action.generic.flags  = QMF_GRAYED;
	s_keys_bfg10k_action.generic.x		= 0;
	s_keys_bfg10k_action.generic.y		= y += 8;
	s_keys_bfg10k_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_bfg10k_action.generic.localdata[0] = ++i;
	s_keys_bfg10k_action.generic.name	= bindnames[s_keys_bfg10k_action.generic.localdata[0]][1];

	s_keys_grenades_action.generic.type	= MTYPE_ACTION;
	s_keys_grenades_action.generic.flags  = QMF_GRAYED;
	s_keys_grenades_action.generic.x		= 0;
	s_keys_grenades_action.generic.y		= y += 8;
	s_keys_grenades_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_grenades_action.generic.localdata[0] = ++i;
	s_keys_grenades_action.generic.name	= bindnames[s_keys_grenades_action.generic.localdata[0]][1];
	
	// zoom
	s_keys_zoom_action.generic.type	= MTYPE_ACTION;
	s_keys_zoom_action.generic.flags  = QMF_GRAYED;
	s_keys_zoom_action.generic.x		= 0;
	s_keys_zoom_action.generic.y		= y += 8;
	s_keys_zoom_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_zoom_action.generic.localdata[0] = ++i;
	s_keys_zoom_action.generic.name	= bindnames[s_keys_zoom_action.generic.localdata[0]][1];
	// end

	s_keys_help_computer_action.generic.type	= MTYPE_ACTION;
	s_keys_help_computer_action.generic.flags  = QMF_GRAYED;
	s_keys_help_computer_action.generic.x		= 0;
	s_keys_help_computer_action.generic.y		= y += 8;
	s_keys_help_computer_action.generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_help_computer_action.generic.localdata[0] = ++i;
	s_keys_help_computer_action.generic.name	= bindnames[s_keys_help_computer_action.generic.localdata[0]][1];

	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_attack_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_change_weapon_action );
	// previous weapon
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_change_weapon_action_previous );
	//
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_walk_forward_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_backpedal_action );
	/* legacy shit!
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_turn_left_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_turn_right_action );
	*/
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_run_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_step_left_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_step_right_action );
	/* legacy shit!
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_sidestep_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_look_up_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_look_down_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_center_view_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_mouse_look_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_keyboard_look_action );
	*/
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_move_up_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_move_down_action );

	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_inventory_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_inv_use_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_inv_drop_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_inv_prev_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_inv_next_action );

	// weapons
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_blaster_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_shotgun_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_super_shotgun_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_machinegun_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_chaingun_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_grenade_launcher_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_rocket_launcher_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_hyperblaster_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_railgun_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_bfg10k_action );
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_grenades_action );

	//Zoom
	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_zoom_action );
	// end

	Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_help_computer_action );
	
	Menu_SetStatusBar( &s_keys_menu, "enter to change, backspace to clear" );
	Menu_Center( &s_keys_menu );
}

static void Keys_MenuDraw (void)
{
	Menu_AdjustCursor( &s_keys_menu, 1 );
	Menu_Draw( &s_keys_menu );
}

static const char *Keys_MenuKey( int key )
{
	menuaction_s *item = ( menuaction_s * ) Menu_ItemAtCursor( &s_keys_menu );

	if ( bind_grab )
	{	
		if ( key != K_ESCAPE && key != '`' )
		{
			char cmd[1024];

			Com_sprintf (cmd, sizeof(cmd), "bind \"%s\" \"%s\"\n", Key_KeynumToString(key), bindnames[item->generic.localdata[0]][0]);
			Cbuf_InsertText (cmd);
		}
		
		Menu_SetStatusBar( &s_keys_menu, "enter to change, backspace to clear" );
		bind_grab = false;
		return menu_out_sound;
	}

	switch ( key )
	{
	case K_KP_ENTER:
	case K_ENTER:
		KeyBindingFunc( item );
		return menu_in_sound;
	case K_BACKSPACE:		// delete bindings
	case K_DEL:				// delete bindings
	case K_KP_DEL:
		M_UnbindCommand( bindnames[item->generic.localdata[0]][0] );
		return menu_out_sound;
	default:
		return Default_MenuKey( &s_keys_menu, key );
	}
}

void M_Menu_Keys_f (void)
{
	Keys_MenuInit();
	M_PushMenu( Keys_MenuDraw, Keys_MenuKey );
}


/*
=======================================================================

CONTROLS MENU

=======================================================================
*/
static cvar_t *win_noalttab;
extern cvar_t *in_joystick;

// disable cinematics
extern cvar_t *cl_hack_nocinematics;
//

static menuframework_s	s_options_menu;
static menuaction_s		s_options_defaults_action;
static menuaction_s		s_options_customize_options_action;
static menuslider_s		s_options_sensitivity_slider;
static menulist_s		s_options_freelook_box;
static menulist_s		s_options_noalttab_box;
// no cinematics
static menulist_s		s_options_nocinematics_box;
//smooth mouse
static menulist_s		s_options_mouse_smooth_box;
// hand
static menulist_s		s_options_handedness_box;
// end
static menulist_s		s_options_alwaysrun_box;
static menulist_s		s_options_invertmouse_box;
//static menulist_s		s_options_lookspring_box;
//static menulist_s		s_options_lookstrafe_box;
static menulist_s		s_options_crosshair_box;
static menulist_s		s_options_aimbot_box;	/* BDGN 3.57h: the joke row */
static void AiAimbotFunc (void *unused);
static menuslider_s		s_options_sfxvolume_slider;
static menulist_s		s_options_joystick_box;
static menulist_s		s_options_cdvolume_box;		// BDGN 3.49h: "music": digital (CD) / retro MIDI / off
static menuslider_s		s_options_musicvolume_slider;
static menuaction_s		s_options_soundtrack_action;
static menulist_s s_mididevice_list;
static char s_mididevice_names[257][128];
static const char *s_mididevice_items[258];
static int s_mididevice_ids[257];
static menulist_s		s_options_soundmode_list;	// BDGN 3.49h: original / optimized / MIDI effects / no sound
static int Opt_MusicChoiceFromCvars( void );
static qboolean s_mididevice_scanned;
static void MidiDeviceReset (void);
static int Opt_SoundModeFromCvars( void );

static menulist_s		s_options_quality_list;
static int s_options_scroll;

static menuframework_s s_soundtrack_menu;
static menuaction_s s_soundtrack_rows[15];

static menuaction_s s_soundtrack_stop_action;
static menulist_s s_soundtrack_playback_list;
/* index = s_musicplaylist: repeat track / play next track */
static const char *s_soundtrack_playback_items[] = { "repeat track", "play next track", 0 };
static char *s_soundtrack_files_found[256];
static int s_soundtrack_count;
static int s_soundtrack_first;
static int s_soundtrack_selected;
static int s_soundtrack_line_height;
static cvar_t *s_musicfile;

static void Soundtrack_MenuInit (void);
static void Soundtrack_MenuDraw (void);
static const char *Soundtrack_MenuKey (int key);
static void SoundtrackScan (void);
static void SoundtrackUpdateRows (void);
static void SoundtrackStartSelected (void);

static menulist_s		s_options_compatibility_list;
static menulist_s		s_options_basicsounds_box;	// BDGN 3.51x: moved here from the video effects menu
static menulist_s		s_options_console_action;

// BDGN 3.37: sound / network rows of the Options menu (moved here from the speed menu and the player setup menu)
static menulist_s		s_options_channels_list;
static menulist_s		s_options_rate_list;		// shown in the net options menu
static menulist_s		s_options_packets_list;	// shown in the net options menu
static menuaction_s		s_options_net_action;

static const int opt_channels_table[4]	= { 32, 24, 16, 8 };
static const int opt_rate_table[6]		= { 2500, 3200, 5000, 10000, 25000, 0 };	// 0 = "user defined", the rate cvar is not changed
static const int opt_packets_table[5]	= { 0, 60, 40, 30, 20 };				// 0 = every frame

/* index of the table entry that is nearest to value */
static int Opt_NearestIndex( const int *table, int count, int value )
{
	int i, best = 0, bestdiff = 0x7fffffff;

	for ( i = 0 ; i < count ; i++ )
	{
		int diff = table[i] > value ? table[i] - value : value - table[i];

		if ( diff < bestdiff )
		{
			bestdiff = diff;
			best = i;
		}
	}
	return best;
}

static void OptChannelsFunc( void *unused )
{
	Cvar_SetValue( "s_maxchannels", opt_channels_table[s_options_channels_list.curvalue] );
}

static void OptRateFunc( void *unused )
{
	if ( opt_rate_table[s_options_rate_list.curvalue] != 0 )
		Cvar_SetValue( "rate", opt_rate_table[s_options_rate_list.curvalue] );
}

static void OptPacketsFunc( void *unused )
{
	Cvar_SetValue( "cl_maxpackets", opt_packets_table[s_options_packets_list.curvalue] );
}

static void OptNetOptionsFunc( void *unused )
{
	M_Menu_DownloadOptions_f();
}

static void CrosshairFunc( void *unused )
{
	Cvar_SetValue( "crosshair", s_options_crosshair_box.curvalue );
}

static void JoystickFunc( void *unused )
{
	Cvar_SetValue( "in_joystick", s_options_joystick_box.curvalue );
}

static void CustomizeControlsFunc( void *unused )
{
	M_Menu_Keys_f();
}

//no cinematics
static void NoCinematicsFunc( void *unused )
{
	Cvar_SetValue( "cl_hack_nocinematics", s_options_nocinematics_box.curvalue );
}
//smooth mouse
static void SmoothMouseFunc( void *unused )
{
	Cvar_SetValue( "m_filter", s_options_mouse_smooth_box.curvalue );
}
//-----------

static void AlwaysRunFunc( void *unused )
{
	Cvar_SetValue( "cl_run", s_options_alwaysrun_box.curvalue );
}

static void FreeLookFunc( void *unused )
{
	Cvar_SetValue( "freelook", s_options_freelook_box.curvalue );
}

static void MouseSpeedFunc( void *unused )
{
	Cvar_SetValue( "sensitivity", s_options_sensitivity_slider.curvalue / 2.0F );
	// set default sensitivity and zoom sensitivity
	Cvar_SetValue( "dsensitivity", s_options_sensitivity_slider.curvalue / 2.0F );
	Cvar_SetValue( "zsensitivity", s_options_sensitivity_slider.curvalue / 4.8F );
}

static void NoAltTabFunc( void *unused )
{
	Cvar_SetValue( "win_noalttab", s_options_noalttab_box.curvalue );
}

static float ClampCvar( float min, float max, float value )
{
	if ( value < min ) return min;
	if ( value > max ) return max;
	return value;
}

static void ControlsSetMenuItemValues( void )
{
	s_options_sfxvolume_slider.curvalue		= Cvar_VariableValue( "s_volume" ) * 10;
	s_options_musicvolume_slider.curvalue	= Cvar_VariableValue( "s_musicvolume" ) * 10;
	s_options_cdvolume_box.curvalue 		= Opt_MusicChoiceFromCvars();
	MidiDeviceReset ();
	s_options_soundmode_list.curvalue		= Opt_SoundModeFromCvars();
//	s_options_quality_list.curvalue			= !Cvar_VariableValue( "s_loadas8bit" );	// usless with more then two menu options
	s_options_sensitivity_slider.curvalue	= ( sensitivity->value ) * 2;
	
	//no cinematics
	Cvar_SetValue( "cl_hack_nocinematics", ClampCvar( 0, 1, cl_hack_nocinematics->value ) );
	s_options_nocinematics_box.curvalue		= cl_hack_nocinematics->value;
	//

	//mouse smoothing
	Cvar_SetValue( "m_filter", ClampCvar( 0, 1, m_filter->value ) );
	s_options_mouse_smooth_box.curvalue		= m_filter->value;
	//

	Cvar_SetValue( "cl_run", ClampCvar( 0, 1, cl_run->value ) );
	s_options_alwaysrun_box.curvalue		= cl_run->value;

	s_options_invertmouse_box.curvalue		= m_pitch->value < 0;
/* legacy shit
	Cvar_SetValue( "lookspring", ClampCvar( 0, 1, lookspring->value ) );
	s_options_lookspring_box.curvalue		= lookspring->value;

	Cvar_SetValue( "lookstrafe", ClampCvar( 0, 1, lookstrafe->value ) );
	s_options_lookstrafe_box.curvalue		= lookstrafe->value;
*/
	Cvar_SetValue( "freelook", ClampCvar( 0, 1, freelook->value ) );
	s_options_freelook_box.curvalue			= freelook->value;

	Cvar_SetValue( "crosshair", ClampCvar( 0, CROSSHAIR_COUNT - 1, crosshair->value ) );	/* BDGN 3.57h: 16 crosshairs */
	s_options_crosshair_box.curvalue		= crosshair->value;

	// s_profile
	Cvar_SetValue( "s_profile", ClampCvar( 0, 4, s_profile->value ) );
	s_options_quality_list.curvalue = ((int)s_profile->value + 1) % 5;
	// end

	Cvar_SetValue( "in_joystick", ClampCvar( 0, 1, in_joystick->value ) );
	s_options_joystick_box.curvalue		= in_joystick->value;

	s_options_noalttab_box.curvalue			= win_noalttab->value;
}

static void ControlsResetDefaultsFunc( void *unused )
{
	Cbuf_AddText ("exec default.cfg\n");
	Cbuf_Execute();

	ControlsSetMenuItemValues();
}

static void InvertMouseFunc( void *unused )
{
	Cvar_SetValue( "m_pitch", -m_pitch->value );
}

/* legacy shit
static void LookspringFunc( void *unused )
{
	Cvar_SetValue( "lookspring", !lookspring->value );
}

static void LookstrafeFunc( void *unused )
{
	Cvar_SetValue( "lookstrafe", !lookstrafe->value );
}
*/
static void UpdateVolumeFunc( void *unused )
{
	Cvar_SetValue( "s_volume", s_options_sfxvolume_slider.curvalue / 10 );
}

static void UpdateMusicVolumeFunc( void *unused )
{
	float volume = s_options_musicvolume_slider.curvalue / 10.0f;
	Cvar_SetValue ("s_musicvolume", volume);
	WavMusic_SetVolume (volume);
	MIDI_SetMusicVolume (volume);
}



static void SoundtrackPlaybackFunc (void *unused)
{
	Cvar_SetValue ("s_musicplaylist", s_soundtrack_playback_list.curvalue);
	WavMusic_SetPlayNext (s_soundtrack_playback_list.curvalue != 0);	/* also for the track playing now */
}

static void SoundtrackStopFunc (void *unused)
{
	CDAudio_Stop ();
}

static void SoundtrackSelectFunc (void *unused)
{
	menuaction_s *item = (menuaction_s *)unused;
	int row = (int)(item - s_soundtrack_rows);
	int selected = s_soundtrack_first + row;
	if (row < 0 || row >= 15 || selected < 0 || selected >= s_soundtrack_count)
		return;
	s_soundtrack_selected = selected;
	if (s_musicfile)
		Cvar_Set ("s_musicfile", s_soundtrack_files_found[selected]);
	SoundtrackStartSelected ();
}

static void SoundtrackStartSelected (void)
{
	if (s_soundtrack_selected < 0 || s_soundtrack_selected >= s_soundtrack_count)
		return;
	if (!s_musicfile)
		return;
	Cvar_Set ("s_musicfile", s_soundtrack_files_found[s_soundtrack_selected]);
	Cvar_SetValue ("s_musicdefault", 0);
	Cvar_SetValue ("s_musicmode", 0); /* an explicitly chosen WAV/MP3 replaces MIDI music */
	/* Stop only music playback; ambient and other game sound channels continue. */
	CDAudio_Stop ();
	WavMusic_PlaySelected (s_musicfile->string, Cvar_VariableValue ("s_musicplaylist") != 0);
}

static void SoundtrackUpdateRows (void)
{
	int i, index;
	for (i = 0; i < 15; i++)
	{
		index = s_soundtrack_first + i;
		s_soundtrack_rows[i].generic.type = MTYPE_ACTION;
		s_soundtrack_rows[i].generic.x = -230;
		s_soundtrack_rows[i].generic.y = i * s_soundtrack_line_height;
		s_soundtrack_rows[i].generic.flags = QMF_LEFT_JUSTIFY;
		/* The action cursor must sit just left of the left-justified filename. */
		s_soundtrack_rows[i].generic.cursor_offset = 0;
		s_soundtrack_rows[i].generic.callback = SoundtrackSelectFunc;
		s_soundtrack_rows[i].generic.name = (index >= 0 && index < s_soundtrack_count) ? s_soundtrack_files_found[index] : "";
	}
	if (s_soundtrack_count == 0)
	{
		s_soundtrack_rows[0].generic.name = "No WAV or MP3 files found";
	}
}

static void Soundtrack_MenuInit (void)
{
	int i;
	SoundtrackScan ();
	s_soundtrack_menu.x = viddef.width / 2;
	s_soundtrack_menu.y = viddef.height / 2 - 70;
	s_soundtrack_line_height = (viddef.height < 300) ? 8 : 10;
	s_soundtrack_menu.nitems = 0;
	if (s_soundtrack_selected >= s_soundtrack_count) s_soundtrack_selected = 0;
	s_soundtrack_first = s_soundtrack_selected - 7;
	if (s_soundtrack_first < 0) s_soundtrack_first = 0;
	if (s_soundtrack_first + 15 > s_soundtrack_count && s_soundtrack_count > 15) s_soundtrack_first = s_soundtrack_count - 15;
	SoundtrackUpdateRows ();
	s_soundtrack_menu.cursor = s_soundtrack_count ? s_soundtrack_selected - s_soundtrack_first : 15;
	for (i = 0; i < 15; i++) Menu_AddItem (&s_soundtrack_menu, &s_soundtrack_rows[i]);
	/* One empty row under the list, then "stop playback" over "playback <toggle>", the pair centred on
	   the screen. The toggle's label ends 8 px left of x and its longest value ("play next track",
	   15 chars) starts 16 px right of x, so x = -32 centres it; "stop playback" starts in the value column. */
	s_soundtrack_playback_list.generic.type = MTYPE_SPINCONTROL;
	s_soundtrack_playback_list.generic.x = -32;
	s_soundtrack_playback_list.generic.y = 17 * s_soundtrack_line_height;
	s_soundtrack_playback_list.generic.cursor_offset = -32;	/* the cursor is drawn at menu x + cursor_offset */
	s_soundtrack_playback_list.generic.name = "playback";
	s_soundtrack_playback_list.generic.callback = SoundtrackPlaybackFunc;
	s_soundtrack_playback_list.itemnames = s_soundtrack_playback_items;
	s_soundtrack_playback_list.curvalue = Cvar_VariableValue ("s_musicplaylist") ? 1 : 0;
	s_soundtrack_stop_action.generic.type = MTYPE_ACTION;
	s_soundtrack_stop_action.generic.flags = QMF_LEFT_JUSTIFY;
	s_soundtrack_stop_action.generic.x = 0;		/* text at x - 16: the toggle's value column */
	s_soundtrack_stop_action.generic.y = 16 * s_soundtrack_line_height;
	s_soundtrack_stop_action.generic.name = "stop playback";
	s_soundtrack_stop_action.generic.callback = SoundtrackStopFunc;
	Menu_AddItem (&s_soundtrack_menu, &s_soundtrack_stop_action);		/* cursor 15 */
	Menu_AddItem (&s_soundtrack_menu, &s_soundtrack_playback_list);	/* cursor 16 */
}

static void SoundtrackScan (void)
{
	char names[256][MAX_QPATH];
	int i, count;
	for (i = 0; i < s_soundtrack_count; i++) free (s_soundtrack_files_found[i]);
	s_soundtrack_count = 0;
	count = WavMusic_ListFiles ((char *)names, 256, MAX_QPATH);
	for (i = 0; i < count; i++) s_soundtrack_files_found[i] = strdup (names[i]);
	s_soundtrack_count = count;
	s_soundtrack_selected = 0;
	for (i = 0; i < count; i++)
		if (s_musicfile && !Q_stricmp (s_musicfile->string, s_soundtrack_files_found[i]))
		{
			s_soundtrack_selected = i;
			break;
		}
}

static void Soundtrack_MenuDraw (void)
{
	M_Banner ("m_banner_options");
	Menu_AdjustCursor (&s_soundtrack_menu, 1);
	Menu_Draw (&s_soundtrack_menu);
}

static const char *Soundtrack_MenuKey (int key)
{
	/* The playback toggle has two values: left, right and Enter all flip it. (A plain spin control stops at
	   its last value, Enter does nothing on it, and left would leave for the list, so it could not be
	   switched back to "repeat track".) Up/Down still move between the rows. */
	if (s_soundtrack_menu.cursor == 16 && (key == K_LEFTARROW || key == K_KP_LEFTARROW || key == K_RIGHTARROW
		|| key == K_KP_RIGHTARROW || key == K_ENTER || key == K_KP_ENTER))
	{
		s_soundtrack_playback_list.curvalue = !s_soundtrack_playback_list.curvalue;
		SoundtrackPlaybackFunc (&s_soundtrack_playback_list);
		return menu_move_sound;
	}
	if (key == K_RIGHTARROW || key == K_KP_RIGHTARROW)
	{
		if (s_soundtrack_menu.cursor < 15)
		{
			s_soundtrack_menu.cursor = 15;	/* "stop playback" */
			return menu_move_sound;
		}
	}
	if (key == K_LEFTARROW || key == K_KP_LEFTARROW)
	{
		if (s_soundtrack_menu.cursor >= 15 && s_soundtrack_count > 0)
		{
			s_soundtrack_menu.cursor = s_soundtrack_selected - s_soundtrack_first;
			if (s_soundtrack_menu.cursor < 0) s_soundtrack_menu.cursor = 0;
			if (s_soundtrack_menu.cursor > 14) s_soundtrack_menu.cursor = 14;
			return menu_move_sound;
		}
	}
	if (key == K_UPARROW || key == K_KP_UPARROW || key == K_DOWNARROW || key == K_KP_DOWNARROW)
	{
		int direction = (key == K_UPARROW || key == K_KP_UPARROW) ? -1 : 1;
		int next;
		/* The list reserves 15 rows, but empty rows must never receive focus. */
		if (s_soundtrack_menu.cursor == 15)
		{
			if (direction > 0 || s_soundtrack_count == 0)
			{
				s_soundtrack_menu.cursor = 16;
				return menu_move_sound;
			}
			next = s_soundtrack_count - 1;
		}
		else if (s_soundtrack_menu.cursor == 16)
		{
			if (direction < 0 || s_soundtrack_count == 0)
			{
				s_soundtrack_menu.cursor = 15;
				return menu_move_sound;
			}
			next = 0;
		}
		else
			next = s_soundtrack_first + s_soundtrack_menu.cursor + direction;
		if (next < 0)
		{
			s_soundtrack_menu.cursor = 16;
			return menu_move_sound;
		}
		if (next >= s_soundtrack_count)
		{
			s_soundtrack_menu.cursor = 15;
			return menu_move_sound;
		}
		s_soundtrack_selected = next;
		if (s_musicfile) Cvar_Set ("s_musicfile", s_soundtrack_files_found[next]);
		if (s_soundtrack_selected < s_soundtrack_first) s_soundtrack_first = s_soundtrack_selected;
		if (s_soundtrack_selected >= s_soundtrack_first + 15) s_soundtrack_first = s_soundtrack_selected - 14;
		SoundtrackUpdateRows ();
		s_soundtrack_menu.cursor = s_soundtrack_selected - s_soundtrack_first;
		return menu_move_sound;
	}
	return Default_MenuKey (&s_soundtrack_menu, key);
}

static void SoundtrackMenuFunc (void *unused)
{
	Soundtrack_MenuInit ();
	M_PushMenu (Soundtrack_MenuDraw, Soundtrack_MenuKey);
}

static void MidiDeviceFunc (void *unused)
{
 int device=s_mididevice_ids[s_mididevice_list.curvalue];
 if(device==(int)Cvar_VariableValue("s_mididevice")) return;
 Cvar_SetValue ("s_mididevice", device);
 MIDI_Update ();
}

static void MidiDeviceReset (void)
{
 s_mididevice_scanned=false;
 s_mididevice_items[0]="scan with Enter or arrows";
 s_mididevice_items[1]=NULL;
 s_mididevice_list.itemnames=s_mididevice_items;
 s_mididevice_list.curvalue=0;
 s_mididevice_list.generic.flags=0;
}

static void MidiDeviceScan (void)
{
 int i, count = 1, selected = 0;
 int devices = MIDI_DeviceCount ();
 int preferred = (int)Cvar_VariableValue ("s_mididevice");
 MIDI_DeviceName (-1, s_mididevice_names[0], sizeof(s_mididevice_names[0]));
 s_mididevice_items[0] = s_mididevice_names[0];
 s_mididevice_ids[0] = -1;
 for (i = 0; i < devices && count < (int)(sizeof(s_mididevice_names)/sizeof(s_mididevice_names[0])); i++)
 {
  if (!MIDI_DeviceName (i, s_mididevice_names[count], sizeof(s_mididevice_names[count]))) continue;
  s_mididevice_items[count] = s_mididevice_names[count];
  s_mididevice_ids[count] = i;
  if (i == preferred) selected = count;
  count++;
 }
 s_mididevice_items[count] = NULL;
 s_mididevice_list.itemnames = s_mididevice_items;
 s_mididevice_list.curvalue = selected;
 s_mididevice_scanned = true;
}

void M_RefreshMusic (void)
{
 CDAudio_Stop ();
 if (cls.state == ca_active) CDAudio_Play (atoi(cl.configstrings[CS_CDTRACK]), true);
 else if (MIDI_MusicMode () == 1) MIDI_MenuMusic ();
 else if (MIDI_MusicMode () != 2 && !Cvar_VariableValue ("s_musicdefault") && Cvar_VariableString ("s_musicfile")[0])
  WavMusic_PlaySelected (Cvar_VariableString ("s_musicfile"), Cvar_VariableValue ("s_musicplaylist") != 0);
}
static void UpdateCDVolumeFunc (void *unused)
{
 int v = s_options_cdvolume_box.curvalue;
 Cvar_SetValue ("s_musicmode", v == 4 ? 2 : (v == 2 ? 1 : 0));
 Cvar_SetValue ("s_musicdefault", 1);
 Cvar_SetValue ("cd_nocd", 0);
 Cvar_SetValue ("s_musicsource", v == 0 ? 2 : (v == 3 ? 0 : 1));
 M_RefreshMusic ();
}

static int Opt_SoundModeFromCvars( void )
{
	if ( Cvar_VariableValue( "s_nosound" ) )
		return 3;
	if ( Cvar_VariableValue( "s_pcspeaker" ) == 2 )
		return 5;	/* BDGN 3.58l */
	if ( Cvar_VariableValue( "s_pcspeaker" ) )
		return 4;	/* BDGN 3.58g */
	if ( Cvar_VariableValue( "s_midieffects" ) )
		return 2;
	if ( Cvar_VariableValue( "s_maxchannels" ) <= 16 )
		return 1;
	return 0;
}

static int Opt_MusicChoiceFromCvars( void )
{
 int mode = (int)Cvar_VariableValue ("s_musicmode");
 int source = (int)Cvar_VariableValue ("s_musicsource");
 if (mode == 2) return 4;
 if (mode == 1) return 2;
 return source == 2 ? 0 : (source == 0 ? 3 : 1);
}

void M_SetSoundMode (int v)
{
	// BDGN 3.49h: original / optimized / MIDI effects / no sound
	

	Cvar_SetValue( "s_nosound", v == 3 );
	Cvar_SetValue( "s_midieffects", v == 2 );
	Cvar_Get( "s_pcspeaker", "0", CVAR_ARCHIVE );
	Cvar_SetValue( "s_pcspeaker", v == 4 ? 1 : (v == 5 ? 2 : 0) );	/* BDGN 3.58g: Wolfenstein 3D style effects on the PC speaker;
		3.58l: 5 = the same through the sound card (headphones) */
	if ( v == 0 )
	{
		Cvar_SetValue( "s_mixinterval", 15 );
		Cvar_SetValue( "s_maxchannels", 32 );
	}
	else if ( v == 1 || v == 2 || v == 4 || v == 5 )
	{
		Cvar_SetValue( "s_mixinterval", 15 );
		Cvar_SetValue( "s_maxchannels", 16 );
	}
	S_StopAllSounds();
}

static void UpdateSoundModeFunc( void *unused )
{
 M_SetSoundMode(s_options_soundmode_list.curvalue);
}


static void ConsoleFunc( void *unused )
{
	/*
	** the proper way to do this is probably to have ToggleConsole_f accept a parameter
	*/
	extern void Key_ClearTyping( void );

	if ( cl.attractloop )
	{
		Cbuf_AddText ("killserver\n");
		return;
	}

	Key_ClearTyping ();
	Con_ClearNotify ();

	M_ForceMenuOff ();
	cls.key_dest = key_console;
}
// hand
static void HandednessCallbackSP( void *unused )
{
	Cvar_SetValue( "hand", s_options_handedness_box.curvalue );
}
//
void M_SetSoundQuality (int v)
{
 static const int rates[]={8,11,22,44,4};
 if (v<0 || v>=5) v=1;
 Cvar_SetValue("s_khz",rates[v]);
 Cvar_SetValue("s_loadas8bit",rates[v]<=11);
 Cvar_SetValue("s_profile",v);
}

static void UpdateSoundQualityFunc( void *unused )
{
 M_SetSoundQuality((s_options_quality_list.curvalue + 4) % 5);
 Cvar_SetValue( "s_primary", s_options_compatibility_list.curvalue );

	M_DrawTextBox( 8, 120 - 48, 36, 3 );
	M_Print( 16 + 16, 120 - 48 + 8,  "Restarting the sound system. This" );
	M_Print( 16 + 16, 120 - 48 + 16, "could take up to a minute, so" );
	M_Print( 16 + 16, 120 - 48 + 24, "please be patient." );

	// the text box won't show up unless we do a buffer swap
	re.EndFrame();

	CL_Snd_Restart_f();
}


static void BasicSoundsFunc( void *unused )	// BDGN 3.51x: takes effect at the next sound
{
	Cvar_SetValue( "s_basic_sounds", s_options_basicsounds_box.curvalue );
}

void Options_MenuInit( void )
{
	static const char *cd_music_items[] =
	{
		"MP3", "WAV", "MIDI", "CD Audio", "Off",
		0
	};
	static const char *sound_mode_items[] =
	{
		"original",
		"optimized",
		"MIDI effects",
		"no sound",
		"PC speaker",	/* BDGN 3.58g */
		"PC speaker (sound card)",	/* BDGN 3.58l: the same effects through the sound card / headphones */
		0
	};
	static const char *quality_items[] =
	{
		// doogies code
		//"low", "high", 0
//		"lowest", "low", "high", "highest", 0    // Q2TTM: support 8 KHz audio (lowest = 0/8, low = 1/11, high = 2/22, highest = 3/44)
		"4 kHz 8bit", "8kHz 8bit", "11khz 8bit", "22kHz 16bit", "44kHz 16bit", 0
		// end
	};
	// hand
	static const char *handedness_items[] =
	{
		"right hand", "left hand", "hidden", 0
	};
	// end

	static const char *compatibility_items[] =
	{
		"max compatibility", "max performance", 0
	};

	static const char *yesno_names[] =
	{
		"no",
		"yes",
		0
	};

	static const char *crosshair_names[] =
	{
		"none",
		"cross",
		"dot",
		"angle",
		0
	};

	win_noalttab = Cvar_Get( "win_noalttab", "0", CVAR_ARCHIVE );
	s_musicfile = Cvar_Get ("s_musicfile", "", CVAR_ARCHIVE);
	Cvar_Get ("s_musicplaylist", "0", CVAR_ARCHIVE);
	Cvar_Get ("s_musicdefault", "1", CVAR_ARCHIVE);
	Cvar_Get ("s_musicvolume", "1.0", CVAR_ARCHIVE);
	Cvar_Get ("s_musicsource", "1", CVAR_ARCHIVE);

	/*
	** configure controls menu and menu items
	*/
	s_options_menu.x = viddef.width / 2;
	s_options_menu.y = viddef.height / 2 - 68;
	s_options_menu.nitems = 0;

	s_options_sfxvolume_slider.generic.type	= MTYPE_SLIDER;
	s_options_sfxvolume_slider.generic.x	= 0;
	// move slider/toggle up to make more space for new sliders/toggles
	s_options_sfxvolume_slider.generic.y	= 0;
	s_options_sfxvolume_slider.generic.name	= "effects volume";
	s_options_sfxvolume_slider.generic.callback	= UpdateVolumeFunc;
	s_options_sfxvolume_slider.minvalue		= 0;
	s_options_sfxvolume_slider.maxvalue		= 10;
	s_options_sfxvolume_slider.curvalue		= Cvar_VariableValue( "s_volume" ) * 10;

	s_options_cdvolume_box.generic.type	= MTYPE_SPINCONTROL;
	s_options_cdvolume_box.generic.x		= -56;
	// move slider/toggle up to make more space for new sliders/toggles
	s_options_cdvolume_box.generic.y		= 10;
	s_options_cdvolume_box.generic.name	= "music";
	/* Music changes are applied only when Enter is pressed on this row. */
	s_options_cdvolume_box.generic.callback	= NULL;
	s_options_cdvolume_box.itemnames		= cd_music_items;
	s_options_cdvolume_box.curvalue 		= Opt_MusicChoiceFromCvars();
	s_options_musicvolume_slider.generic.type = MTYPE_SLIDER;
	s_options_musicvolume_slider.generic.x = 0;
	s_options_musicvolume_slider.generic.y = 20;
	s_options_musicvolume_slider.generic.name = "music volume";
	s_options_musicvolume_slider.generic.callback = UpdateMusicVolumeFunc;
	s_options_musicvolume_slider.minvalue = 0;
	s_options_musicvolume_slider.maxvalue = 10;
	s_options_musicvolume_slider.curvalue = Cvar_VariableValue ("s_musicvolume") * 10;
	s_options_soundtrack_action.generic.type = MTYPE_ACTION;
	s_options_soundtrack_action.generic.x = 0;
	s_options_soundtrack_action.generic.y = 30;	/* right under music volume, then an empty row */
	s_options_soundtrack_action.generic.name = "choose soundtrack";
	s_options_soundtrack_action.generic.callback = SoundtrackMenuFunc;
	s_mididevice_list.generic.type = MTYPE_SPINCONTROL;
	s_mididevice_list.generic.x = 0;
	s_mididevice_list.generic.y = 40;
	s_mididevice_list.generic.name = "MIDI output device";
	s_mididevice_list.generic.callback = MidiDeviceFunc;

	s_options_soundmode_list.generic.type	= MTYPE_SPINCONTROL;
	s_options_soundmode_list.generic.x		= 0;
	s_options_soundmode_list.generic.y		= 100;	// BDGN 3.51x: 90 is basic sounds
	s_options_soundmode_list.generic.name	= "sound mode";
	s_options_soundmode_list.generic.callback = UpdateSoundModeFunc;
	s_options_soundmode_list.itemnames		= sound_mode_items;
	s_options_soundmode_list.curvalue		= Opt_SoundModeFromCvars();


	s_options_quality_list.generic.type	= MTYPE_SPINCONTROL;
	s_options_quality_list.generic.x		= 0;
	// move slider/toggle up to make more space for new sliders/toggles
	s_options_quality_list.generic.y		= 50;
	s_options_quality_list.generic.name		= "sound quality";
	s_options_quality_list.generic.callback = UpdateSoundQualityFunc;
	FS_RescanQualityPacks();	/* BDGN 3.55n: sound sets copied into baseq2/sounds/ while the game runs */
	quality_items[4] = FS_HDSoundPackAvailable() ? "44kHz 16bit HD" : "44kHz 16bit";	/* BDGN 3.55g: pak7-hd-sounds44.pak at 44 kHz */
	s_options_quality_list.itemnames		= quality_items;
//	s_options_quality_list.curvalue			= !Cvar_VariableValue( "s_loadas8bit" );
	s_options_quality_list.curvalue = ((int)ClampCvar(0,4,Cvar_VariableValue("s_profile")) + 1) % 5;

	s_options_compatibility_list.generic.type	= MTYPE_SPINCONTROL;
	s_options_compatibility_list.generic.x		= 0;
	// move slider/toggle up to make more space for new sliders/toggles
	s_options_compatibility_list.generic.y		= 60;
	s_options_compatibility_list.generic.name	= "sound compatibility";
	s_options_compatibility_list.generic.callback = UpdateSoundQualityFunc;
	s_options_compatibility_list.itemnames		= compatibility_items;
	s_options_compatibility_list.curvalue		= Cvar_VariableValue( "s_primary" );


	// BDGN 3.51x: basic sounds (fewer effects), right under max sounds at once
	s_options_basicsounds_box.generic.type	= MTYPE_SPINCONTROL;
	s_options_basicsounds_box.generic.x		= 0;
	s_options_basicsounds_box.generic.y		= 90;
	s_options_basicsounds_box.generic.name	= "basic sounds (fewer effects)";
	s_options_basicsounds_box.generic.callback = BasicSoundsFunc;
	s_options_basicsounds_box.itemnames		= yesno_names;
	s_options_basicsounds_box.curvalue		= Cvar_VariableValue( "s_basic_sounds" ) ? 1 : 0;

	// no cinecmatics (under sound mode)
	s_options_nocinematics_box.generic.type = MTYPE_SPINCONTROL;
	s_options_nocinematics_box.generic.x	= 0;
	s_options_nocinematics_box.generic.y	= 110;
	s_options_nocinematics_box.generic.name	= "no cinematics";
	s_options_nocinematics_box.generic.callback = NoCinematicsFunc;
	s_options_nocinematics_box.itemnames = yesno_names;
	//

	// add mosue smoothing
	s_options_mouse_smooth_box.generic.type = MTYPE_SPINCONTROL;
	s_options_mouse_smooth_box.generic.x	= 0;
	s_options_mouse_smooth_box.generic.y	= 120;
	s_options_mouse_smooth_box.generic.name	= "smooth mouse";
	s_options_mouse_smooth_box.generic.callback = SmoothMouseFunc;
	s_options_mouse_smooth_box.itemnames = yesno_names;
	//

	s_options_sensitivity_slider.generic.type	= MTYPE_SLIDER;
	s_options_sensitivity_slider.generic.x		= 0;
	s_options_sensitivity_slider.generic.y		= 130;
	s_options_sensitivity_slider.generic.name	= "mouse speed";
	s_options_sensitivity_slider.generic.callback = MouseSpeedFunc;
	s_options_sensitivity_slider.minvalue		= 2;
	s_options_sensitivity_slider.maxvalue		= 22;

	s_options_alwaysrun_box.generic.type = MTYPE_SPINCONTROL;
	s_options_alwaysrun_box.generic.x	= 0;
	s_options_alwaysrun_box.generic.y	= 140;
	s_options_alwaysrun_box.generic.name	= "always run";
	s_options_alwaysrun_box.generic.callback = AlwaysRunFunc;
	s_options_alwaysrun_box.itemnames = yesno_names;

	s_options_invertmouse_box.generic.type = MTYPE_SPINCONTROL;
	s_options_invertmouse_box.generic.x	= 0;
	s_options_invertmouse_box.generic.y	= 150;
	s_options_invertmouse_box.generic.name	= "invert mouse";
	s_options_invertmouse_box.generic.callback = InvertMouseFunc;
	s_options_invertmouse_box.itemnames = yesno_names;

	// hand
	s_options_handedness_box.generic.type	= MTYPE_SPINCONTROL;
	s_options_handedness_box.generic.x		= 0;
	s_options_handedness_box.generic.y		= 160;
	s_options_handedness_box.generic.name		= "weapon";
	s_options_handedness_box.generic.callback = HandednessCallbackSP;
	s_options_handedness_box.itemnames		= handedness_items;
	s_options_handedness_box.curvalue		= Cvar_VariableValue( "hand" );
	// end

/* legacy shit
	s_options_lookspring_box.generic.type = MTYPE_SPINCONTROL;
	s_options_lookspring_box.generic.x	= 0;
	s_options_lookspring_box.generic.y	= 100;
	s_options_lookspring_box.generic.name	= "lookspring";
	s_options_lookspring_box.generic.callback = LookspringFunc;
	s_options_lookspring_box.itemnames = yesno_names;

	s_options_lookstrafe_box.generic.type = MTYPE_SPINCONTROL;
	s_options_lookstrafe_box.generic.x	= 0;
	s_options_lookstrafe_box.generic.y	= 110;
	s_options_lookstrafe_box.generic.name	= "lookstrafe";
	s_options_lookstrafe_box.generic.callback = LookstrafeFunc;
	s_options_lookstrafe_box.itemnames = yesno_names;
*/
	s_options_freelook_box.generic.type = MTYPE_SPINCONTROL;
	s_options_freelook_box.generic.x	= 0;
	s_options_freelook_box.generic.y	= 180;
	s_options_freelook_box.generic.name	= "free look";
	s_options_freelook_box.generic.callback = FreeLookFunc;
	s_options_freelook_box.itemnames = yesno_names;

	s_options_crosshair_box.generic.type = MTYPE_SPINCONTROL;
	s_options_crosshair_box.generic.x	= 0;
	s_options_crosshair_box.generic.y	= 190;
	s_options_crosshair_box.generic.name	= "crosshair";
	s_options_crosshair_box.generic.callback = CrosshairFunc;
	s_options_crosshair_box.itemnames = crosshair_menu_names;	/* BDGN 3.57h: 16, the chosen one is drawn to the right */
/*
	s_options_noalttab_box.generic.type = MTYPE_SPINCONTROL;
	s_options_noalttab_box.generic.x	= 0;
	s_options_noalttab_box.generic.y	= 120;
	s_options_noalttab_box.generic.name	= "disable alt-tab";
	s_options_noalttab_box.generic.callback = NoAltTabFunc;
	s_options_noalttab_box.itemnames = yesno_names;
*/
	{	/* BDGN 3.57h */
		static const char *aimbot_names[] = { "off", "ON", 0 };
		s_options_aimbot_box.generic.type = MTYPE_SPINCONTROL;
		s_options_aimbot_box.generic.x = 0;
		s_options_aimbot_box.generic.y = 210;
		s_options_aimbot_box.generic.name = "AI aimbot";
		s_options_aimbot_box.generic.callback = AiAimbotFunc;
		s_options_aimbot_box.generic.statusbar = "artificial intelligence that aims for you";
		s_options_aimbot_box.itemnames = aimbot_names;
		s_options_aimbot_box.curvalue = 0;
	}
	s_options_joystick_box.generic.type = MTYPE_SPINCONTROL;
	s_options_joystick_box.generic.x	= 0;
	s_options_joystick_box.generic.y	= 200;
	s_options_joystick_box.generic.name	= "use joystick";
	s_options_joystick_box.generic.callback = JoystickFunc;
	s_options_joystick_box.itemnames = yesno_names;

	s_options_customize_options_action.generic.type	= MTYPE_ACTION;
	s_options_customize_options_action.generic.x		= 0;
	s_options_customize_options_action.generic.y		= 230;
	s_options_customize_options_action.generic.name	= "customize controls";
	s_options_customize_options_action.generic.callback = CustomizeControlsFunc;

	s_options_defaults_action.generic.type	= MTYPE_ACTION;
	s_options_defaults_action.generic.x		= 0;
	s_options_defaults_action.generic.y		= 240;
	s_options_defaults_action.generic.name	= "reset defaults";
	s_options_defaults_action.generic.callback = ControlsResetDefaultsFunc;

	s_options_console_action.generic.type	= MTYPE_ACTION;
	s_options_console_action.generic.x		= 0;
	s_options_console_action.generic.y		= 250;
	s_options_console_action.generic.name	= "go to console";
	s_options_console_action.generic.callback = ConsoleFunc;

	// BDGN 3.37: max sounds at once, and the link to the net options sub-menu
	{
		static const char *channels_items[] = { "32 (default)", "24", "16", "8", 0 };
		int ch = (int)Cvar_VariableValue( "s_maxchannels" );

		s_options_channels_list.generic.type = MTYPE_SPINCONTROL;
		s_options_channels_list.generic.x = 0;
		s_options_channels_list.generic.y = 80;
		s_options_channels_list.generic.name = "max sounds at once";
		s_options_channels_list.generic.callback = OptChannelsFunc;
		s_options_channels_list.itemnames = channels_items;
		s_options_channels_list.curvalue = Opt_NearestIndex( opt_channels_table, 4, ch > 0 ? ch : 32 );

		s_options_net_action.generic.type = MTYPE_ACTION;
		s_options_net_action.generic.x = 0;
		s_options_net_action.generic.y = 220;
		s_options_net_action.generic.name = "net options";
		s_options_net_action.generic.callback = OptNetOptionsFunc;
	}

	ControlsSetMenuItemValues();

	Menu_AddItem( &s_options_menu, ( void * ) &s_options_sfxvolume_slider );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_cdvolume_box );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_musicvolume_slider );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_soundtrack_action );
	Menu_AddItem( &s_options_menu, ( void * ) &s_mididevice_list );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_quality_list );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_compatibility_list );
	s_options_scroll=0;
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_channels_list );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_basicsounds_box );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_soundmode_list );
	//no cinematics
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_nocinematics_box );
	//smoot mouse
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_mouse_smooth_box );
	//
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_sensitivity_slider );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_alwaysrun_box );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_invertmouse_box );
	// hand
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_handedness_box );
	// end
/* legacy shit
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_lookspring_box );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_lookstrafe_box );
*/
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_freelook_box );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_crosshair_box );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_joystick_box );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_aimbot_box );	/* BDGN 3.57h */
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_net_action );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_customize_options_action );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_defaults_action );
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_console_action );
}

/* BDGN 3.57h: "AI aimbot" - a joke. Setting it on crashes the game (a fatal error with a message); nothing is saved,
   so the next start is normal. */
static void AiAimbotFunc (void *unused)
{
	if (!s_options_aimbot_box.curvalue)
		return;
	s_options_aimbot_box.curvalue = 0;
	Com_Error (ERR_FATAL, "AI aimbot: the neural network became self-aware, watched you aim for 0.3 seconds\n"
		"and decided to stop all life on this computer.\n\nQuake II has crashed. Nice try.");
}

void Options_MenuDraw (void)
{
	int w,h,top=viddef.height/12;
	re.DrawGetPicSize(&w,&h,"m_banner_options");
	if (h<0) h=0;
	re.DrawPic(viddef.width/2-w/2,top,"m_banner_options");
	Menu_DrawScrolled(&s_options_menu,top+h+8,viddef.height-20,&s_options_scroll);
	{	/* BDGN 3.57h: the chosen crosshair, to the right of its row */
		int iy=s_options_crosshair_box.generic.y, height=viddef.height-20-(top+h+8);
		if (s_options_crosshair_box.curvalue>0 && iy>=s_options_scroll && iy<=s_options_scroll+height-8)
			SCR_DrawCrosshairShape(s_options_crosshair_box.curvalue,
				s_options_menu.x+s_options_crosshair_box.generic.x+16+16*8+16,
				s_options_menu.y+iy+4, viddef.height>=480?2:1);
	}
}

const char *Options_MenuKey( int key )
{
 if (key==K_MWHEELUP) key=K_UPARROW;
 if (key==K_MWHEELDOWN) key=K_DOWNARROW;
 if(Menu_ItemAtCursor(&s_options_menu)==&s_mididevice_list) {
  if(!s_mididevice_scanned) {
   switch(key) {
   case K_ENTER: case K_KP_ENTER: case K_MOUSE1:
   case K_LEFTARROW: case K_KP_LEFTARROW:
   case K_RIGHTARROW: case K_KP_RIGHTARROW:
    MidiDeviceScan();
    return menu_move_sound;
   }
  }
 }
	if ((key == K_ENTER || key == K_KP_ENTER) && Menu_ItemAtCursor (&s_options_menu) == &s_options_cdvolume_box)
	{
		UpdateCDVolumeFunc (NULL);
		return menu_move_sound;
	}
	return Default_MenuKey( &s_options_menu, key );
}

void M_Menu_Options_f (void)
{
	Options_MenuInit();
	M_PushMenu ( Options_MenuDraw, Options_MenuKey );
}


/*
=======================================================================

SPEED & NETWORK MENU (BDGN 3.37)

=======================================================================
*/

#include "tweak_settings.h"
#include "../qcommon/q3e_3dnow.h"
const bdgn_tweak_t bdgn_tweaks[] = {
 { "DRAW PATHS","model batching","gl_fastdraw","gl_modelbatch_auto","0",4,5,{0,0,1,2,3},1,4,4,1,1,{"auto (3DNow!)","off","triangles","strips/fans","strips + byte",NULL} },
 { "DRAW PATHS","particle batching","gl_fastparticles","gl_particlebatch_auto","0",4,3,{0,0,1},1,2,2,1,2,{"auto (3DNow!)","off","on",NULL} },
 { "DRAW PATHS","world batching","gl_batchworld",NULL,"0",4,2,{0,1},0,1,1,1,0,{"off","on",NULL} },
 { "DRAW PATHS","4 MB video card limits","vid_lowvram",NULL,"2",4,3,{0,1,2},0,2,2,TK_GL,0,{"off","on","auto",NULL} },	/* BDGN 3.59s: ref_gl R_LowVram: 2D sprite atlas and 2D weapon cache 1 MB each; auto = bdgn_vramcheck says 4 MB or less. 3.59w: the retained ON choice (Ultra / PSYCHO presets, bdgn_fastest) is auto, not on: PSYCHO put it on and the Voodoo3 lost 65 % with textures (MEASURED, Chromatic v3.59u) */
 { "DRAW PATHS","single-colour walls in one run","gl_flatstate",NULL,"1",4,2,{0,1},0,1,1,1,0,{"off (per wall)","on",NULL} },	/* BDGN 3.59p: ref_gl gl_rsurf.c R_DrawFlatSurface; r_drawflat (PSYCHO); benchmark: CRASH HUNT */
 { "DRAW PATHS","SSE packed light maps","gl_sse_packed",NULL,"1",4,2,{0,1},1,1,1,TK_GL|TK_SSE|TK_MENUONLY,0,{"off","on",NULL} },	/* BDGN 3.58w: ref_gl gl_simd.c Q3S_LightmapScale4; benchmark: bench_config.h */
 { "DRAW PATHS","sync every frame","gl_finish",NULL,"0",-1,2,{0,1},0,1,0,1,0,{"off","on",NULL} },
 { "DRAW PATHS","OpenGL multitexture","gl_ext_multitexture",NULL,"0",4,2,{0,1},0,0,1,3,0,{"off","on",NULL} },	/* BDGN 3.52i: off is the default and the fast choice (3dfx presets: on) */
 { "SIMPLER LIGHTING","fullbright (all objects)","gl_fastworld",NULL,"0",6,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "LIGHTING","shadows","gl_shadows",NULL,"0",0,5,{0,1,2,3,4},0,0,0,1,0,{"off","original","soft contact","from lights","light volumes",NULL} },	/* 3.55m: from lights; 3.55ai: light volumes (gl_shadowvol.c) */
 { "SIMPLER LIGHTING","dynamic lights","gl_dynamic",NULL,"1",6,2,{0,1},1,1,0,1,0,{"off","on",NULL} },
 { "SIMPLER LIGHTING","skip unseen lights","gl_dlight_cull",NULL,"1",6,2,{0,1},1,1,1,1,0,{"off","on",NULL} },	/* BDGN 3.51p: same picture */
 { "SIMPLER LIGHTING","dynamic light distance","gl_dlight_maxdist",NULL,"0",6,4,{0,1024,768,512},0,3,3,1,0,{"unlimited","1024","768","512",NULL} },	/* BDGN 3.51p: changes the picture */
 { "SIMPLER LIGHTING","flat model lighting","gl_flatmodels",NULL,"0",6,2,{0,1},0,1,1,1,0,{"off","on",NULL} },
 { "SIMPLER LIGHTING","static light styles","cl_staticlightstyles",NULL,"0",6,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "SIMPLER LIGHTING","grey light maps (software)","sw_test_gray_lightmaps",NULL,"0",6,2,{0,1},0,0,0,TK_RESTART|TK_MENUONLY,0,{"off","on",NULL} },	/* 3.60c: was on the TEST Low End page; TK_MENUONLY keeps it out of presets, defaults and the benchmark, as page 3 was */
 { "LIGHTING","per-pixel lighting","gl_pixel_lighting",NULL,"0",0,4,{0,1,2,4},0,0,0,TK_GL|TK_MENUONLY,0,{"off","1 light","2 lights","4 lights",NULL} },	/* BDGN 3.55i: moved from the Video page; shown only with renderer support */
 { "LIGHTING","surface relief","gl_pixel_bump",NULL,"1",0,2,{0,1},1,1,1,TK_GL|TK_MENUONLY,0,{"off","on",NULL} },
 { "SIMPLER WORLD","single colour textures","r_drawflat",NULL,"0",6,4,{0,2,3,1},0,1,1,1,0,{"off","material colours","grayscale","random colours",NULL} },
 { "SIMPLER WORLD","simple sky","gl_fastsky",NULL,"0",6,2,{0,1},0,1,1,1,0,{"off","on",NULL} },
 { "SIMPLER WORLD","compact walls (software, extra low textures)","sw_test_compact_walls",NULL,"0",6,2,{0,1},0,0,0,TK_RESTART|TK_MENUONLY,0,{"off","on",NULL} },	/* 3.60c: was on the TEST Low End page; TK_MENUONLY keeps it out of presets, defaults and the benchmark, as page 3 was */
 { "SIMPLER WORLD","water without waves","gl_fastwater",NULL,"0",6,2,{0,1},0,1,1,1,0,{"off","on",NULL} },
 { "DETAIL & DISTANCE","low detail far models","gl_lowdetail",NULL,"0",6,4,{0,1000,600,300},0,3,3,1,0,{"off","far (1000)","medium (600)","near (300)",NULL} },
 { "DETAIL & DISTANCE","render scale (software)","sw_render_scale",NULL,"100",6,3,{100,75,50},0,0,0,TK_MENUONLY,0,{"100%","75%","50%",NULL} },	/* 3.60c: was on the TEST Low End page; TK_MENUONLY keeps it out of presets, defaults and the benchmark, as page 3 was */
 { "DETAIL & DISTANCE","draw distance (software)","sw_drawdist",NULL,"0",6,3,{0,2,4},0,0,0,TK_MENUONLY,0,{"unlimited","medium","short",NULL} },	/* 3.60c: was on the TEST Low End page; TK_MENUONLY keeps it out of presets, defaults and the benchmark, as page 3 was */
 { "DETAIL & DISTANCE","maximum texture size","gl_maxtexsize",NULL,"0",6,4,{0,128,64,32},0,3,3,3,0,{"off","128","64","32",NULL} },
 /* BDGN 3.58v (owner 2026-11-21): texture and skin detail steps, model animation smoothing and the view weapon, each
    with a note. Menu rows only: the benchmark measures them through client/bench_config.h, the presets set them in
    win32/vid_menu.c VID_ApplyTierDesign (the view weapon is never hidden by a preset) */
 { "DETAIL & DISTANCE","texture detail (mip)","gl_picmip",NULL,"0",6,4,{0,1,2,3},0,3,3,TK_GL|TK_RESTART|TK_MENUONLY,0,{"full","1/2","1/4","1/8",NULL} },
 { "DETAIL & DISTANCE","player skin detail (mip)","gl_playermip",NULL,"0",6,4,{0,1,2,3},0,3,3,TK_GL|TK_RESTART|TK_MENUONLY,0,{"full","1/2","1/4","1/8",NULL} },
 { "DETAIL & DISTANCE","model animation smoothing","r_lerpmodels",NULL,"1",6,2,{1,0},0,1,1,TK_GL|TK_MENUONLY,0,{"on","off",NULL} },
 { "DETAIL & DISTANCE","view weapon","cl_gun",NULL,"1",6,2,{1,0},0,0,0,TK_MENUONLY,0,{"shown","hidden",NULL} },
 { "VIEW & MATH","widescreen FOV","cl_fovaspect",NULL,"1",4,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "VIEW & MATH","fast sqrt","fastsqrt",NULL,"0",4,2,{0,1},0,1,1,0,0,{"off (exact)","Q_rsqrt",NULL} },	/* BDGN 3.51aut: 3DNow! rsqrt removed (slower on the K6-III+) */
 { "VIEW & MATH","fast sqrt move/game/cull","fastsqrt2",NULL,"0",4,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "VIEW & MATH","CPU profile","cpu_profile",NULL,"1",4,2,{0,1},1,1,1,TK_MENUONLY,0,{"off (generic)","auto",NULL} },	/* BDGN 3.58w: client/cl_main.c CL_CPUProfileApply; benchmark: client/bench_config.h */
 { "VIEW & MATH","fixed 100 Hz physics","cl_async",NULL,"0",4,2,{0,1},0,1,0,TK_MENUONLY,0,{"off (3.21)","on",NULL} },	/* BDGN 3.58q: moves of exactly 10 ms, the picture as fast as the cap below allows (cl_input.c CL_AsyncSendCmd); not a benchmark case: a timedemo has no moves to send */
 { "VIEW & MATH","frame rate cap","r_maxfps",NULL,"0",4,12,{0,60,75,100,120,144,165,200,240,280,360,500},0,0,0,TK_MENUONLY,0,{"none","60","75","100","120","144","165","200","240","280","360","500",NULL} },	/* BDGN 3.58q: with fixed physics on (cl_maxfps paces the frames when it is off) */
 { "ANIMATIONS","classic 2d sprites mode","cl_classic2d",NULL,"0",2,4,{0,1,2,3},0,1,1,0,0,{"off","on","on (brightskins)","low-end",NULL} },	/* BDGN 3.51bct: 2 = the animated enemies as one-colour bright figures (cl_simpleenemies 4) */	/* BDGN 3.51x: one toggle for 4-direction enemies at 12.5 fps (32x32, far animation auto), 2d items and sprite explosions (BDGN_ClassicSprites); replaces the "2d items" row and the "simple enemies..." page link */
 { "ANIMATIONS","Doom mode","cl_retromode",NULL,"0",2,2,{0,1},0,1,1,TK_RESTART|TK_MENUONLY,0,{"off","on",NULL} },	/* BDGN 3.58w: BDGN_RetroModeSet; benchmark cases @doom */
 { "ANIMATIONS","Wolfenstein 3D mode","cl_retromode",NULL,"0",2,3,{0,2,3},0,1,2,TK_RESTART|TK_MENUONLY,0,{"off","on","low-res",NULL} },	/* BDGN 3.58w: @wolf, @wolflow */
 { "ANIMATIONS","Duke Nukem 3D mode","cl_retromode",NULL,"0",2,2,{0,4},0,1,1,TK_RESTART|TK_MENUONLY,0,{"off","on",NULL} },	/* BDGN 3.60i (owner 2026-11-23, "for the lulz", no benchmark case): BDGN_RetroModeSet 4 */
 { "ANIMATIONS","Duke 3D view (Build)","cl_yshear",NULL,"0",2,2,{0,1},0,1,1,TK_MENUONLY,0,{"off","on",NULL} },	/* BDGN 3.60i: looking up / down moves the horizon (qcommon/yshear.h); ref_gl and ref_soft (r_yshear_support) */
 { "ANIMATIONS","fastest preset (EXTREME)","cl_fastestpreset",NULL,"0",2,2,{0,1},0,1,1,TK_RESTART|TK_MENUONLY,0,{"off","on",NULL} },	/* 3.60i: owner graphics/audio profile, 60% view, no distance/fog cutoff; existing half-screen window and 75% render scale. BDGN 3.59y: win32/vid_menu.c VID_FastestPresetToggle: lowerThen32mb EXTREME (72), the fastest MEASURED preset; off brings the earlier preset back; benchmark: Lupusrex Pentium 133 (EXTREME cases) */
 { "ANIMATIONS","retro 2D weapons","cl_retroweapons",NULL,"0",2,2,{0,1},0,1,1,TK_MENUONLY,0,{"off","on",NULL} },	/* BDGN 3.58w: bdgn_weapons_retro.pak (qcommon/weapon_sprite.h WS_RetroStyle) */
 { "ANIMATIONS","blue enemies","r_spritetint",NULL,"0",2,2,{0,1},0,1,1,TK_MENUONLY,0,{"off","on",NULL} },	/* BDGN 3.58w: qcommon/duke_playback.h; Wolfenstein 3D mode turns it on */
 { "ANIMATIONS","Wolfenstein 3D uniforms","cl_enemyskins",NULL,"0",2,2,{0,1},0,0,0,TK_RESTART|TK_MENUONLY,0,{"original","WWII",NULL} },	/* BDGN 3.58y: only in Wolfenstein 3D mode; bdgn_enemyskins.pak (qcommon/files.c, qcommon/duke_playback.h) */
 { "ANIMATIONS","2d enemy animation rate","cl_sprite_rate",NULL,"1",2,2,{1,2},0,0,0,0,0,{"12.5 fps","6.25 fps",NULL} },	/* BDGN 3.51z: one rate for enemy and weapon sprites (BDGN_SpriteRate: cl_simpleenemy_anim, cl_weaponsprite_anim); replaces the "sprite animation" row of the removed page */
 { "ANIMATIONS","weapon look","cl_weaponsprites",NULL,"0",-1,2,{0,1},0,1,1,0,0,{"3D model","2D sprites",NULL} },	/* BDGN 3.51z: under 2d sprites animation rate (video effects page) */	/* BDGN 3.51ab: hidden; classic 2d sprites mode sets the 2D weapon */
 { "ANIMATIONS","static 2D sprite weapon","cl_weapon_simple",NULL,"0",2,2,{0,1},0,1,1,0,0,{"off","on",NULL} },	/* BDGN 3.51aa: renamed from "simple weapon", moved under weapon look */
 { "ANIMATIONS","2d enemy directions","cl_duke_directions",NULL,"4",2,2,{4,2},0,0,1,0,0,{"4 directions","front / back",NULL} },
 { "ANIMATIONS","slower far animation","cl_simpleenemy_lod",NULL,"1",2,3,{0,1,2},0,1,1,0,0,{"off","auto","3.125 fps far",NULL} },	/* BDGN 3.51p: duke_lod.h; 3.51s: on (auto) by default, near-still far away; 3.52h: beyond 1024 also front/back pictures only */
 { "ANIMATIONS","freeze enemy 3D animations","cl_staticenemies",NULL,"0",2,2,{0,1},0,0,0,0,0,{"off","on",NULL} },	/* BDGN 3.51x: under classic 2d sprites mode */
 { "ANIMATIONS","static 2d enemies","cl_simple2denemies",NULL,"0",2,5,{0,1,3,2,4},0,1,1,0,0,{"off","on","on (brightskins)","low-res","low-res brightskins",NULL} },	/* BDGN 3.51bct: 3/4 = brightskins (cl_simpleenemies 2); low-res (2/4) in the look of classic mode (32x32 tiles, GL_NEAREST) */	/* BDGN 3.51ast: 2 = the same flat 2D models with 64x64 pictures instead of 256x256 */	/* BDGN 3.51y: enemies as flat 2D models (cl_simpleenemies 1), over classic 2d sprites mode; replaces the "simple enemies" row of the removed page (console: cl_simpleenemies 0-4) */
 { "ANIMATIONS","bright skins (3D enemies)","r_brightskins",NULL,"0",2,2,{0,1},0,1,1,1,0,{"off","on",NULL} },	/* BDGN 3.51bct: shown on the effects page */
 { "ANIMATIONS","freeze map / pickup rotation","cl_staticmap","cl_noitemrotate","0",2,2,{0,1},0,1,1,0,3,{"off","on",NULL} },	/* BDGN 3.51ag: under freeze enemy 3D animations */
 { "ANIMATIONS","sprite lighting (TEST)","cl_spritelight",NULL,"0",2,2,{0,1},0,0,1,0,0,{"off","on",NULL} },	/* BDGN 3.52r: 2D enemies/items lit by the world (RF_LITSPRITE) */
/* BDGN 3.55ac: the effects page sorted - what makes the game look better first (weapon effects by level, then the
   picture effects), then what makes it simpler / faster, then decorations. Rows are only shown when this computer
   can use them (BDGN_TweakVisible), so a page is short on old hardware. */
 { "WEAPON EFFECTS","effect level","cl_fxlevel",NULL,"0",1,6,{0,1,3,2,4,5},0,0,0,TK_MENUONLY,0,{"original","HD","HD 4K","HD 3D","HD 3D 4K","custom",NULL} },	/* BDGN 3.56r: levels 3 (HD 4K) and 4 (HD 3D 4K) */	/* BDGN 3.55ab: sets the rows below (cl_gore.c CL_FXLevel_Apply); "custom" is shown when they were changed one by one */
 { "WEAPON EFFECTS","HD sprite effects","cl_hdfx",NULL,"0",1,3,{0,1,2},0,0,0,TK_GL|TK_MENUONLY,0,{"off","HD","HD 4K",NULL} },	/* BDGN 3.56r: 2 = pak8-hdfx-4k.pak */	/* level HD: pak8-hdfx.pak (3.55s), any OpenGL driver */
 { "WEAPON EFFECTS","HD bullet holes / marks","cl_decals",NULL,"0",1,3,{0,1,2},0,0,0,TK_GL|TK_MENUONLY,0,{"off","HD","HD 4K",NULL} },	/* BDGN 3.56r: 2 = pak8-hdgore-4k.pak */	/* level HD: pak8-hdgore.pak (3.55y) */
 { "WEAPON EFFECTS","3D explosions","cl_3dexplo",NULL,"0",1,2,{0,1},0,0,0,TK_GL|TK_MENUONLY,0,{"off","on",NULL} },	/* level HD 3D, modern drivers: rockets, grenades (3.55ab), barrels (3.55aa) */
 { "WEAPON EFFECTS","3D BFG ball / explosion","cl_3dbfg",NULL,"0",1,2,{0,1},0,0,0,TK_GL|TK_MENUONLY,0,{"off","on",NULL} },
 { "WEAPON EFFECTS","3D comet blaster bolts","cl_3dblaster",NULL,"0",1,2,{0,1},0,0,0,TK_GL|TK_MENUONLY,0,{"off","on",NULL} },
 { "WEAPON EFFECTS","3D railgun","cl_3drail",NULL,"0",1,2,{0,1},0,0,0,TK_GL|TK_MENUONLY,0,{"off","on",NULL} },
 /* BDGN 3.56r: colour of the 3D railgun's core / spirals (client/cl_gore.c, models/hdgore/rail_s<N>.md2) */
 { "WEAPON EFFECTS","3D railgun colours","cl_3drail_colors",NULL,"0",1,10,{0,1,2,3,4,5,6,7,8,9},0,0,0,TK_GL|TK_MENUONLY,0,{"red / blue orange green","red / yellow","green / red","blue / yellow","yellow / purple","purple / green","cyan / magenta","white / blue","orange / cyan","white / rainbow",NULL} },
 { "WEAPON EFFECTS","3D gibs and blood","cl_hdgibs",NULL,"0",1,2,{0,1},0,0,0,TK_GL|TK_MENUONLY,0,{"off","on",NULL} },
 { "WEAPON EFFECTS","3D smoke","cl_hdsmoke",NULL,"0",1,2,{0,1},0,0,0,TK_GL|TK_MENUONLY,0,{"off","on",NULL} },
 /* BDGN 3.56y: how much smoke rises from lava with 3D smoke on (client/cl_fx3d.c FX_LavaSmoke) */
 { "WEAPON EFFECTS","lava smoke","cl_lavasmoke",NULL,"3",1,8,{1,2,3,4,5,6,8,10},2,2,2,TK_GL|TK_MENUONLY,0,{"1x","2x","3x","4x","5x","6x","8x","10x",NULL} },
 /* BDGN 3.56z: lava spouts up out of the pools, with a ring on the lava (client/cl_fx3d.c FX_LavaBursts) */
 { "WEAPON EFFECTS","lava bursts","cl_lavabursts",NULL,"0",1,5,{0,1,2,3,4},0,0,0,TK_GL|TK_MENUONLY,0,{"off","1x","2x","3x","4x",NULL} },
 /* BDGN 3.56z: the hyperblaster throws out its spent energy cell every 50 shots, the BFG drops one per shot */
 { "WEAPON EFFECTS","energy cells","cl_cells",NULL,"0",1,2,{0,1},0,0,0,TK_MENUONLY,0,{"off","on",NULL} },
 { "WEAPON EFFECTS","3D muzzle flashes","cl_hdmuzzle",NULL,"0",1,2,{0,1},0,0,0,TK_GL|TK_MENUONLY,0,{"off","on",NULL} },
 /* BDGN 3.56s: brass casings (machine gun, chaingun) and red shotgun shells fly out of the gun (client/cl_fx3d.c); every renderer */
 { "WEAPON EFFECTS","shell casings","cl_shells",NULL,"0",1,2,{0,1},0,0,0,TK_MENUONLY,0,{"off","on",NULL} },
 { "WEAPON EFFECTS","modern grenade smoke","cl_grenadesmoke",NULL,"0",1,2,{0,1},0,1,0,TK_GL,0,{"off","on",NULL} },	/* BDGN 3.57h; 3.58d: from Testing to effects (owner) */
 { "WEAPON EFFECTS","smoke puff after fire smoke","cl_explosmoke",NULL,"0",1,2,{0,1},0,1,0,TK_GL,0,{"off","on",NULL} },	/* BDGN 3.57h; 3.58d: from Testing to effects (owner) */
 { "WEAPON EFFECTS","3D heat distortion","cl_heatdistort",NULL,"0",1,2,{0,1},0,0,0,TK_GL|TK_MENUONLY,0,{"off","on",NULL} },	/* extras on top of HD 3D */
 { "WEAPON EFFECTS","3D effect textures","cl_fx3d_tex",NULL,"0",1,4,{0,2,1,3},0,0,0,TK_GL|TK_MENUONLY,0,{"standard","standard 4K","AI (Stable Diffusion)","AI 4K",NULL} },	/* BDGN 3.56r: 2 / 3 = the 4K packs */
 { "PICTURE EFFECTS","3D surface structure","gl_relief",NULL,"0",1,2,{0,1},0,0,0,TK_GL|TK_MENUONLY,0,{"off","on",NULL} },	/* BDGN 3.55ah: relief from the loaded texture set (gl_render_scale.c GL_DrawRelief) */
 { "PICTURE EFFECTS","bloom","gl_bloom",NULL,"0",1,3,{0,1,2},0,0,0,TK_GL|TK_MENUONLY,0,{"off","low","high",NULL} },	/* BDGN 3.55i: moved from the Video page */
 { "PICTURE EFFECTS","ambient occlusion","gl_ssao",NULL,"0",1,3,{0,1,2},0,0,0,TK_GL|TK_MENUONLY,0,{"off","low","high",NULL} },
 { "PICTURE EFFECTS","tone mapping","gl_tonemap",NULL,"0",1,2,{0,1},0,0,0,TK_GL|TK_MENUONLY,0,{"off","filmic",NULL} },
 { "PICTURE EFFECTS","colour grading","gl_colour_grade",NULL,"0",1,3,{0,1,2},0,0,0,TK_GL|TK_MENUONLY,0,{"off","warm","cool",NULL} },
 { "WATER","modern water","gl_water_surface",NULL,"0",1,2,{0,1},0,0,0,TK_GL|TK_MENUONLY,0,{"off","waves",NULL} },
 { "WATER","water ripples / splashes","gl_water_interaction",NULL,"0",1,2,{0,1},0,0,0,TK_GL|TK_MENUONLY,0,{"off","on",NULL} },
 { "WATER","water reflections","gl_water_reflect",NULL,"0",1,3,{0,1,2},0,0,0,TK_GL|TK_MENUONLY,0,{"off","half size","full size",NULL} },	/* BDGN 3.55o */
 { "SIMPLER EFFECTS","blood / bullet hits","cl_bloodfx","cl_impactfx","0",6,4,{0,1,2,3},0,2,2,0,3,{"on","reduced","off","transparent ball",NULL} },	/* BDGN 3.51ag: names say what is shown (0 = on/original, 2 = off/removed) */
 { "SIMPLER EFFECTS","simple explosions","cl_static_rockets","cl_static_bfg","0",6,3,{0,1,2},0,1,1,0,3,{"off","on","classic 2d",NULL} },	/* BDGN 3.51v: rockets, grenades, BFG; 2 = sprites/bdgn_fx; 3.51w: also blaster bolts and hits (cl_simpleblaster, BDGN_TweakSet) */
 { "SIMPLER EFFECTS","simple railgun","cl_simple_rail",NULL,"0",6,2,{0,1},0,1,1,0,0,{"off","on",NULL} },	/* BDGN 3.51w: the blaster moved to "simple explosions" */
 { "SIMPLER EFFECTS","remove rocket/blaster/grenade trails","cl_notrails",NULL,"0",6,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "SIMPLER EFFECTS","remove smoke / flashes","cl_nosmoke",NULL,"0",6,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "SIMPLER EFFECTS","remove steam / smoke / flies","cl_nosteam","g_noflies","0",6,2,{0,1},0,1,1,0,3,{"off","on",NULL} },
 { "SIMPLER EFFECTS","remove gibs / debris","g_gibmode",NULL,"0",6,3,{0,1,2},0,2,2,32,0,{"off","reduced","on",NULL} },
 { "SIMPLER / FASTER","sprite hits (TEST)","cl_sprite_hits",NULL,"0",-1,2,{0,1},0,1,1,0,0,{"off","on",NULL} },	/* BDGN 3.51ai: test toggle for FPS comparison (cl_fx.c CL_SpriteHit) */
 { "SIMPLER / FASTER","sprite trails (TEST)","cl_sprite_trails",NULL,"0",-1,2,{0,1},0,1,1,0,0,{"off","on",NULL} },	/* BDGN 3.51ah: test toggle for FPS comparison (cl_fx.c CL_SpriteTrail) */
 { "DECORATIONS","hide banners","cl_nobanners",NULL,"0",6,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "DECORATIONS","hide decorative corpses","cl_nodecorpses",NULL,"0",6,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "DECORATIONS","hide spawn pads","cl_nospawnpads",NULL,"0",6,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "SOUND","basic sounds (fewer effects)","s_basic_sounds",NULL,"0",-1,2,{0,1},0,1,1,0,0,{"off","on",NULL} },	/* BDGN 3.51x: moved to the Options menu (under max sounds at once); kept here for defaults/benchmark */
 /* BDGN 3.51aut/3.51bat: no rows for 3DNow!, AMD's kernels, cpu_auto, SSE or MMX: they are automatic */
 { "SOUND","sound mix interval","s_mixinterval",NULL,"15",-1,2,{0,15},1,1,1,0,0,{"every frame","15 ms",NULL} },
 /* BDGN 3.51q: first-person weapon as pre-rendered 2D sprites (bdgn_weapons.pak); visual only */
 /* BDGN 3.57d: page 7 "old-school modes" (Video -> FASTER): the modes from old games, for the software renderer (sw_*),
    for BDGN's OpenGL renderer (gl_*) and for both (cl_*, viewsize). Every row is shown only where it works
    (BDGN_TweakVisible); all are benchmark cases. Before 3.57d they were on the testing page. */
 { "LIGHT","Doom light (stepped, per surface)","sw_doomlight",NULL,"0",7,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "LIGHT","Doom light (stepped, per surface)","gl_doomlight",NULL,"0",7,2,{0,1},0,1,1,TK_RESTART,0,{"off","on",NULL} },	/* BDGN 3.57d: ref_gl gl_light.c */
 { "LIGHT","Blake Stone shading (no light maps)","sw_blakelight",NULL,"0",7,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "LIGHT","Blake Stone shading (no light maps)","gl_blakelight",NULL,"0",7,2,{0,1},0,1,1,TK_RESTART,0,{"off","on",NULL} },	/* BDGN 3.57d */
 { "LIGHT","light flash instead of dynamic lights","cl_lightflash",NULL,"0",7,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "LIGHT","Quake 1 fog (fade to black)","sw_q1fog",NULL,"0",7,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "LIGHT","Quake 1 fog (fade to black)","gl_q1fog",NULL,"0",7,2,{0,1},0,1,1,0,0,{"off","on",NULL} },	/* BDGN 3.57d: ref_gl gl_rmain.c */
 { "PICTURE","half vertical resolution","sw_halfrows",NULL,"0",7,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "PICTURE","Doom low detail (half columns)","sw_halfcols",NULL,"0",7,3,{0,1,2},0,1,2,0,0,{"off","half (Doom low)","quarter (potato)",NULL} },	/* 3.57c: 2 = FastDoom potato */
 { "PICTURE","flat floors (Wolf3D style)","sw_flatfloors",NULL,"0",7,4,{0,1,2,3},0,0,0,0,0,{"off","floors and ceilings","and far walls","all walls (flatter)",NULL} },	/* software renderer (ref_soft/r_edge.c) */
 { "PICTURE","Build floors (affine, Duke 3D)","sw_buildfloors",NULL,"0",7,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "PICTURE","translucent surfaces","sw_stipplealpha",NULL,"0",7,3,{0,1,2},0,1,2,0,0,{"blended","stipple","solid",NULL} },	/* BDGN 3.57c: ref_soft/r_poly.c */
 { "PICTURE","translucent surfaces","gl_solidalpha",NULL,"0",7,2,{0,1},0,1,1,0,0,{"blended","solid",NULL} },	/* BDGN 3.57d: ref_gl gl_rsurf.c */
 { "PICTURE","Quake 1 particles (solid, max 256)","sw_q1particles",NULL,"0",7,2,{0,1},0,1,1,0,0,{"off","on",NULL} },	/* BDGN 3.57b */
 { "PICTURE","Quake 1 particles (solid, max 256)","gl_q1particles",NULL,"0",7,2,{0,1},0,1,1,0,0,{"off","on",NULL} },	/* BDGN 3.57d: ref_gl gl_rmain.c */
 { "PICTURE","Wolfenstein palette (125 colours)","sw_wolfpalette",NULL,"0",7,2,{0,1},0,1,0,TK_MENUONLY,0,{"off","on",NULL} },
 { "VIEW","locked view + autoaim (Doom)","cl_lockview",NULL,"0",7,2,{0,1},0,1,1,0,0,{"off","on",NULL} },
 { "VIEW","smaller view (viewsize)","viewsize",NULL,"100",7,3,{100,90,80},0,1,2,0,0,{"full","90%","80%",NULL} },	/* BDGN 3.57c */
 { "OBJECTS","far enemies as 2D sprites","cl_far2d",NULL,"0",7,5,{0,384,512,768,1024},0,2,2,0,0,{"off","beyond 384","beyond 512","beyond 768","beyond 1024",NULL} },
 { "OBJECTS","object draw distance","cl_objdist",NULL,"0",7,5,{0,512,768,1024,1536},0,0,0,TK_MENUONLY,0,{"off","512","768","1024","1536",NULL} },	/* BDGN 3.57c: cl_ents.c CL_ObjectTooFar; 3.60f (owner 2026-11-22: weapons, items and enemies must always be seen): off in every preset (on/speed 0, TK_MENUONLY: not in Ultra/PSYCHO through bdgn_fastest); MEASURED 0 % gain (v3.58w) */
 /* BDGN 3.57d: sun and god rays make the game look better: on the lighting page (Video -> BETTER LOOKING) */
 { "SUN & GOD RAYS","sun and lamp shadows","gl_sun",NULL,"0",0,2,{0,1},0,0,0,0,0,{"off","on",NULL} },	/* BDGN 3.55ak: gl_sun.c; with gl_shadows 3 or 4 */
 { "SUN & GOD RAYS","god rays","gl_godrays",NULL,"0",0,3,{0,1,2},0,0,0,0,0,{"off","soft","strong",NULL} },	/* BDGN 3.55ak: gl_render_scale.c GL_DrawGodRays */
 /* BDGN 3.55ai: Video -> "advanced..." (3.60c: was "testing..."): loading, console, online, server, low spec models */
 { "SOFTWARE","window upscale (render scale)","sw_upscale",NULL,"1",5,3,{0,1,2},1,1,1,0,0,{"Windows","BDGN odd steps","BDGN always",NULL} },	/* BDGN 3.58b */
 { "LOW SPEC MODELS","simpler 3D models","cl_simplemodels",NULL,"0",5,4,{0,1,2,3},0,3,1,TK_RESTART,0,{"off","simple","simpler","simplest",NULL} },	/* BDGN 3.57l: bdgn_simplemodels.pak, qcommon/files.c FS_LoadFile */
 { "SOUND","mono sound","s_mono",NULL,"0",5,2,{0,1},0,1,1,0,0,{"off (stereo)","on",NULL} },	/* BDGN 3.57m: win32/snd_win.c, snd_restart */
 { "SOUND","sound quality","s_khz",NULL,"11",5,5,{4,8,11,22,44},2,2,2,TK_MENUONLY,0,{"4 kHz","8 kHz","11 kHz","22 kHz","44 kHz",NULL} },	/* 3.60c: was on the TEST Low End page; TK_MENUONLY keeps it out of presets, defaults and the benchmark, as page 3 was */
 { "LOW SPEC 3D","simpler models + maps + LOD","cl_lowspec3d",NULL,"0",5,2,{0,1},0,1,1,TK_RESTART,0,{"off","on",NULL} },	/* BDGN 3.57s: cl_ents.c CL_LowSpec3DSync */
 { "LOW SPEC MAPS","simpler maps (wall/ceiling detail)","cl_simpleworld",NULL,"0",5,2,{0,1},0,1,1,TK_RESTART,0,{"off","on",NULL} },	/* BDGN 3.57o: bdgn_simpleworld.pak, win32/vid_dll.c VID_RendererLoadFile */
 { "LOW SPEC MODELS","model LOD by distance","cl_modellod",NULL,"0",5,5,{0,1,2,3,4},0,2,1,TK_RESTART,0,{"off (rows below)","light","medium","strong","max",NULL} },	/* BDGN 3.57o: cl_ents.c CL_ModelLodSync */
 { "LOW SPEC MODELS","simplest models far away","cl_simplemodels_far",NULL,"0",5,5,{0,512,768,1024,1536},0,1,1,TK_RESTART,0,{"off","beyond 512","beyond 768","beyond 1024","beyond 1536",NULL} },	/* BDGN 3.57m: cl_ents.c CL_FarModel */
 { "LOW SPEC MODELS","2D enemies as simple 3D models (OpenGL)","r_duke_simple3d",NULL,"0",5,2,{0,1},0,1,1,TK_GL,0,{"off (sprites)","on",NULL} },	/* BDGN 3.59g: qcommon/duke_playback.h R_DrawDuke; with simpler models on */
 { "LOADING","remember missing files (map loads)","fs_misscache",NULL,"1",5,2,{0,1},0,1,1,0,0,{"off (3.21)","on",NULL} },	/* BDGN 3.59f: qcommon/files.c; same files found, fewer disk lookups; benchmark: LOAD_MS / LOAD_PHASES */
 { "LOADING","skip unused HD effects (map loads)","cl_hdload_lazy",NULL,"1",5,2,{0,1},0,1,1,0,0,{"off (load always)","on",NULL} },	/* BDGN 3.59o: cl_tent.c CL_HDAssetsUnused; classic 2D / software skip HD sprites, 3D effects, HD gore; benchmark: CRASH HUNT, LOAD_PHASES */
 { "LOADING","page everything into RAM after a load","r_pagein",NULL,"2",5,3,{0,1,2},0,2,2,0,0,{"off","on (3.21)","auto (off on low RAM)",NULL} },	/* BDGN 3.60b: ref_soft r_model.c R_PageInWanted, client/snd_dma.c; benchmark: Lupusrex Pentium 133 */
 { "ONLINE","chat ignore list (ignore / ignoretext)","cl_ignore",NULL,"1",5,2,{0,1},1,1,1,TK_MENUONLY,0,{"off","on",NULL} },	/* BDGN 3.59f (as Q2PRO): cl_main.c CL_ChatIgnored; client only, works on every 3.21 server */
 { "DEMOS","demo playback speed","cl_demospeed",NULL,"100",5,5,{25,50,100,200,400},2,2,2,TK_MENUONLY,0,{"25%","50%","100% (3.21)","200%","400%",NULL} },	/* BDGN 3.59f: cl_main.c CL_DemoControlFrame; also demopause, demoseek, demoinfo */
 { "INPUT","raw mouse input (Windows XP and later)","in_rawmouse",NULL,"1",5,2,{0,1},0,1,1,TK_MENUONLY,0,{"off (Windows mouse)","on",NULL} },	/* BDGN 3.59z (Q2PRO/Yamagi): win32/in_win.c, RegisterRawInputDevices; hidden on Windows 95/98/ME/NT4/2000 without it */
 { "CONSOLE","save command history","con_history",NULL,"1",5,2,{0,1},0,1,1,TK_MENUONLY,0,{"off (3.21)","on",NULL} },	/* BDGN 3.59z (Q2PRO): client/keys.c, bdgn_history.txt; Tab lists matches, map and demo names, cvar values; Ctrl+R searches */
 { "CONSOLE","time stamps","con_timestamps",NULL,"0",5,2,{0,1},0,1,1,TK_MENUONLY,0,{"off (3.21)","on",NULL} },	/* BDGN 3.59z (Q2PRO): client/console.c */
 { "CONSOLE","chat filter","cl_chatfilter",NULL,"0",5,3,{0,1,2},0,1,1,TK_MENUONLY,0,{"off (3.21)","clean text","hide chat",NULL} },	/* BDGN 3.59z (Q2PRO): client/cl_parse.c */
 { "CONSOLE","write config.cfg only when changed","cfg_writechanged",NULL,"1",5,2,{0,1},0,1,1,TK_MENUONLY,0,{"off (3.21)","on",NULL} },	/* BDGN 3.59z (Q2PRO): client/cl_main.c CL_ConfigReplaceIfChanged */
 { "SERVER","command flood limit (your server)","sv_cmdflood",NULL,"30",5,3,{0,30,60},1,1,1,TK_MENUONLY,0,{"off (3.21)","30 per second","60 per second",NULL} },	/* BDGN 3.59z: server/sv_user.c, R1Q2/Q2PRO */
 { "SERVER","userinfo flood limit (your server)","sv_uiflood",NULL,"8",5,3,{0,8,16},1,1,1,TK_MENUONLY,0,{"off (3.21)","8 per 10 s","16 per 10 s",NULL} },	/* BDGN 3.59z */
 { "VIEW WEAPON","2D weapon animation","cl_weaponsprite_anim",NULL,"2",-1,1,{2},0,0,1,0,0,{"6.25 fps",NULL} },	/* BDGN 3.52h: 6.25 only */	/* BDGN 3.51z: hidden; set by 2d sprites animation rate */
};
const int bdgn_tweak_count = sizeof(bdgn_tweaks)/sizeof(bdgn_tweaks[0]);

/* Reset the whole catalogue, including off-screen/other-page settings. */
static void BDGN_EnemyLook (void);
void BDGN_TweakDefaults (void)
{
 int i;
 if (Cvar_VariableValue("cl_retromode")) BDGN_RetroModeSet(0);	/* BDGN 3.58w */
 Cvar_SetValue("cl_weaponlights",1);
 Cvar_SetValue("cl_lowend_sprites",0);
 for(i=0;i<bdgn_tweak_count;i++) {
  if (bdgn_tweaks[i].flags&TK_MENUONLY) continue;
  Cvar_Set((char *)bdgn_tweaks[i].cvar,(char *)bdgn_tweaks[i].default_value);
  if(bdgn_tweaks[i].aux) Cvar_SetValue((char *)bdgn_tweaks[i].aux,0);
 }
 BDGN_EnemyLook ();	/* BDGN 3.52f: cl_simpleenemies follows the reset rows (it is not a row itself) */
}

/* Capabilities belong to the loaded DLL, not to its file name. Older glam DLLs
   keep their old feature limits; CPU/batching paths remain renderer-specific. */
int BDGN_RendererSupports (const char *name)
{
 static const char *glam[] = {
  "gl_finish", "gl_ext_multitexture", "gl_shadows", "gl_dynamic", "gl_flatmodels",
  "r_drawflat", "gl_fastsky", "gl_fastwater", "cl_staticmap", "gl_lowdetail",
  "gl_maxtexsize", "r_brightskins", NULL
 };
 static const char *soft[] = {
  "gl_dynamic", "gl_flatmodels", "r_drawflat", "gl_fastsky", "gl_fastwater",
  "cl_staticmap", "gl_lowdetail", "gl_maxtexsize", "r_brightskins",
  "gl_picmip", "gl_polyblend", "r_lerpmodels", "vid_gamma", "gl_modulate", NULL
 };
 const char *ref = Cvar_VariableString ("vid_ref");
 int i;
 if (!strcmp(ref,"gl")) return 1;
 if ((!strcmp(ref,"soft") || (!strcmp(ref,"swam") && Cvar_VariableValue("swam_bdgn_features")>=3512)) && Cvar_VariableValue("sw_bdgn_features") >= 3518) {
  if (!strcmp(name,"gl_modulate")) return Cvar_VariableValue("sw_bdgn_features") >= 3519;
  for(i=0;soft[i];i++) if(!strcmp(name,soft[i])) return 1;
  return 0;
 }
 if (strcmp(ref,"glam") || Cvar_VariableValue("glam_bdgn_features") < 3516) return 0;
 for (i=0;glam[i];i++) if (!strcmp(name,glam[i])) return 1;
 return 0;
}

/* BDGN 3.55i: a row is shown only when the loaded renderer and driver support it (no greyed rows for missing features) */
int BDGN_TweakVisible (int index)
{
 const bdgn_tweak_t *t=&bdgn_tweaks[index];
 int post=(int)Cvar_VariableValue("bdgn_gl_post_available");
 if (t->menu<0) return 0;
 if ((t->flags & TK_GL) && !BDGN_RendererSupports(t->cvar)) return 0;
 if (!strcmp(t->cvar,"in_rawmouse")) return Cvar_VariableValue("in_rawmouse_available") != 0;	/* 3.59z: Windows XP and later */
 if (!strcmp(t->cvar,"con_timestamps") || !strcmp(t->cvar,"cl_chatfilter")) return 1;
 if (!strcmp(t->cvar,"cl_enemyskins")) {	/* 3.58y: only with the skin pak */
  static int have=-1;
  if (have<0) have=FS_LoadFile("sprites/anim32ww2/a041.dsa",NULL)>0;
  return have && (Cvar_VariableValue("cl_retromode")==2 || Cvar_VariableValue("cl_retromode")==3);	/* only in Wolfenstein 3D mode */
 }
 if (!strcmp(t->cvar,"cl_yshear") || (!strcmp(t->cvar,"cl_retromode") && t->values[1]==4))
  return Cvar_VariableValue("r_yshear_support")>=1;	/* BDGN 3.60i: the Duke Nukem 3D view: ref_gl and ref_soft only */
 if (!strcmp(t->cvar,"cl_retroweapons")) {	/* 3.58w: only with the retro weapon pak and a renderer that draws 2D weapons */
  static int have=-1;
  if (have<0) have=FS_LoadFile("sprites/weap/doom/v_blast.dwa",NULL)>0;
  return have && Cvar_VariableValue("r_weaponsprite_support")>=1;
 }
 if (!strcmp(t->cvar,"gl_sse_packed") && (!Q3E_CPUHasSSE() || Q_stricmp(Cvar_VariableString("vid_ref"),"gl"))) return 0;	/* 3.58w */
 if (!strcmp(t->cvar,"gl_pixel_lighting") || !strcmp(t->cvar,"gl_pixel_bump"))
  return Cvar_VariableValue("bdgn_gl_pixel_lighting_capable")!=0 && !Q_stricmp(Cvar_VariableString("vid_ref"),"gl");
 if (!strcmp(t->cvar,"gl_sun"))
  return Cvar_VariableValue("bdgn_gl_soft_shadow_capable")!=0 && !Q_stricmp(Cvar_VariableString("vid_ref"),"gl");
 if (!strcmp(t->cvar,"gl_godrays"))
  return Cvar_VariableValue("bdgn_gl_godrays_capable")!=0 && !Q_stricmp(Cvar_VariableString("vid_ref"),"gl");
 if (!strcmp(t->cvar,"cl_simplemodels") || !strcmp(t->cvar,"cl_simplemodels_far") || !strcmp(t->cvar,"cl_modellod") || !strcmp(t->cvar,"cl_lowspec3d"))	/* BDGN 3.57l: only when bdgn_simplemodels.pak is there (not in vanilla) */
 {
  static int have_simple=-1;	/* looked up once: the pak is opened at start */
  if (Cvar_VariableValue("fs_vanilla")) return 0;	/* vanilla searches only id's paks */
  if (have_simple<0) have_simple=FS_LoadFile("models/simple1/monsters/infantry/tris.md2",NULL)>0;
  return have_simple;
 }
 if (!strcmp(t->cvar,"cl_simpleworld"))	/* BDGN 3.57o: only when bdgn_simpleworld.pak is there (not in vanilla) */
 {
  static int have_world=-1;
  if (Cvar_VariableValue("fs_vanilla")) return 0;
  if (have_world<0) have_world=FS_LoadFile("maps/simple/base1.hdr",NULL)>0;
  return have_world;
 }
 if (!strcmp(t->cvar,"sw_asmspans")) return !Q_stricmp(Cvar_VariableString("vid_ref"),"soft");
 if (!strcmp(t->cvar,"sw_upscale")) return !Q_stricmp(Cvar_VariableString("vid_ref"),"soft") && !Cvar_VariableValue("fs_vanilla");	/* 3.58b */
 if (!strcmp(t->cvar,"sw_flatfloors")) return !Q_stricmp(Cvar_VariableString("vid_ref"),"soft");	/* BDGN 3.55ai */
 if (!strcmp(t->cvar,"sw_doomlight") || !strcmp(t->cvar,"sw_buildfloors") || !strcmp(t->cvar,"sw_q1fog") ||
     !strcmp(t->cvar,"sw_blakelight") || !strcmp(t->cvar,"sw_halfrows") || !strcmp(t->cvar,"sw_wolfpalette") ||
     !strcmp(t->cvar,"sw_halfcols") || !strcmp(t->cvar,"sw_q1particles") ||
     !strcmp(t->cvar,"sw_stipplealpha"))
  return !Q_stricmp(Cvar_VariableString("vid_ref"),"soft") && Cvar_VariableValue("sw_oldschool_capable");	/* BDGN 3.57 */
 if (!strcmp(t->cvar,"gl_doomlight") || !strcmp(t->cvar,"gl_blakelight") || !strcmp(t->cvar,"gl_q1fog") ||
     !strcmp(t->cvar,"gl_q1particles") || !strcmp(t->cvar,"gl_solidalpha"))
  return !Q_stricmp(Cvar_VariableString("vid_ref"),"gl") && Cvar_VariableValue("gl_oldschool_capable");	/* BDGN 3.57d: BDGN's ref_gl only */
 if (!strcmp(t->cvar,"gl_relief"))
  return Cvar_VariableValue("bdgn_gl_relief_capable")!=0 && !Q_stricmp(Cvar_VariableString("vid_ref"),"gl");
 if (!strcmp(t->cvar,"gl_bloom")) return (post&2)!=0;
 if (!strcmp(t->cvar,"gl_ssao")) return (post&4)!=0;
 if (!strcmp(t->cvar,"gl_tonemap")) return (post&16)!=0;
 if (!strcmp(t->cvar,"gl_colour_grade")) return (post&32)!=0;
 if (!strcmp(t->cvar,"cl_fxlevel")) return CL_FXLevel_Max()>=1;	/* BDGN 3.55ab */
 if (!strcmp(t->cvar,"cl_hdfx"))
  return Cvar_VariableValue("bdgn_hdfx_available")!=0 && !Q_stricmp(Cvar_VariableString("vid_ref"),"gl");
 if (!strcmp(t->cvar,"cl_3drail_colors")) return CL_FX3D_CvarAvailable("cl_3drail")==1;	/* BDGN 3.56r */
 if (!strcmp(t->cvar,"cl_shells")) return CL_Shells_Available();	/* BDGN 3.56s */
 if (!strcmp(t->cvar,"cl_lavasmoke")) return CL_FX3D_CvarAvailable("cl_hdsmoke")==1;	/* BDGN 3.56y */
 if (!strcmp(t->cvar,"cl_lavabursts")) return CL_FX3D_CvarAvailable("cl_hdsmoke")==1 && (Cvar_VariableValue("bdgn_lavabursts_available") || FS_LoadFile("models/hdgore/lava_burst1.md2",NULL)>0);	/* BDGN 3.56z */
 if (!strcmp(t->cvar,"cl_cells")) return CL_Cells_Available();	/* BDGN 3.56z */
 if (CL_FX3D_CvarAvailable(t->cvar)>=0) return CL_FX3D_CvarAvailable(t->cvar);	/* BDGN 3.55t: driver and pak decide */
 if (!strcmp(t->cvar,"gl_water_surface") || !strcmp(t->cvar,"gl_water_interaction"))
  return Cvar_VariableValue("bdgn_gl_water_surface_capable")!=0 && !Q_stricmp(Cvar_VariableString("vid_ref"),"gl");
 if (!strcmp(t->cvar,"gl_water_reflect"))
  return Cvar_VariableValue("bdgn_gl_water_reflect_capable")!=0 && !Q_stricmp(Cvar_VariableString("vid_ref"),"gl");
 return 1;
}

/* BDGN 3.51x: classic 2d sprites mode. On: enemies as 4-direction sprites (cl_simpleenemies 3) animated at 12.5 fps
   (unless "freeze enemy animations" is on, or the renderer has no animation support), 32x32 library (64x64 when only
   that one is installed), far animation auto and 2d items. Simple explosions start off when entering Classic
   and can be selected independently. Off restores 3D enemies and items. */
/* Static sprites override Classic's directional animation in every resolution. */
static void BDGN_EnemyLook (void)
{
 int simple = (int)Cvar_VariableValue ("cl_simple2denemies");
 int classic = (int)Cvar_VariableValue ("cl_classic2d");
 Cvar_SetValue ("cl_simpleenemies", simple ? (simple >= 3 ? 2 : 1) : (classic ? (classic == 2 ? 4 : 3) : 0));
}
/* BDGN 3.52f: cl_simpleenemies follows the two menu rows also when they were set without the menu (presets, defaults,
   config files, console); called by CL_Frame when cl_simple2denemies or cl_classic2d changed */
void BDGN_SyncEnemyLook (void)
{
 BDGN_EnemyLook ();
}
/* BDGN 3.51z: 2d sprites animation rate: one rate (1 = 12.5, 2 = 6.25 fps) for weapon sprites and animated enemy
   sprites; frozen or static enemies (cl_simpleenemy_anim 0) stay so */
static int BDGN_ClassicDefaultRate(int mode)
{
 return mode==3 ? 2 : 1;
}

static int BDGN_RateValue (void)
{
 return Cvar_VariableValue ("cl_sprite_rate") == 2 ? 2 : 1;
}
static void BDGN_SpriteRate (void)
{
 Cvar_SetValue ("cl_weaponsprite_anim", 2);	/* BDGN 3.52h: the 2D weapon always runs at 6.25 */
 if (Cvar_VariableValue ("cl_simpleenemy_anim")) Cvar_SetValue ("cl_simpleenemy_anim", BDGN_RateValue ());
}
static void BDGN_ClassicSprites (int on, qboolean was_on)
{
 if (on) {
  Cvar_SetValue ("cl_simple2denemies", 0);
  Cvar_SetValue ("cl_staticenemies", 0);
  BDGN_EnemyLook ();
  if (!Cvar_VariableValue ("cl_staticenemies") && Cvar_VariableValue ("r_duke_anim_support") >= 2)
   Cvar_SetValue ("cl_simpleenemy_anim", BDGN_RateValue ());
  Cvar_SetValue ("cl_weaponsprites", Cvar_VariableValue("r_weaponsprite_support")>=1);
  Cvar_SetValue ("cl_weaponsprite_anim", 2);	/* BDGN 3.52h: always 6.25 */
  Cvar_SetValue ("cl_simpleenemy_lod", 1);
  Cvar_SetValue ("cl_simpleitems", 1);
  if (!was_on) {
   Cvar_SetValue ("cl_static_rockets", 0);
   Cvar_SetValue ("cl_static_bfg", 0);
   Cvar_SetValue ("cl_simpleblaster", 0);
  }
  Cvar_SetValue ("cl_sprite_trails", 1);	/* 3.51ao: sprite trails and sprite hits are part of classic mode (no menu rows) */
  Cvar_SetValue ("cl_sprite_hits", 1);
  Cvar_SetValue ("cl_simple_rail", 0);	/* Use the Classic rail sprite effect by default. */
 } else {
  BDGN_EnemyLook ();
  Cvar_SetValue ("cl_simpleenemy_anim", 0);
  Cvar_SetValue ("cl_weaponsprites", 0);	/* 3.51z */
  Cvar_SetValue ("cl_simpleitems", 0);
  Cvar_SetValue ("cl_sprite_trails", 0);
  Cvar_SetValue ("cl_sprite_hits", 0);
 }
}
int BDGN_TweakAllowed (int index, int choice)
{
 const bdgn_tweak_t *t = &bdgn_tweaks[index];
 if (choice < 0 || choice >= t->count) return 0;
 if (!strcmp(t->cvar,"cl_fxlevel")) return CL_FXLevel_Allowed(t->values[choice]);	/* BDGN 3.55ab; 3.56r: by level, "custom" (5) cannot be chosen */
 if ((!strcmp(t->cvar,"cl_hdfx") || !strcmp(t->cvar,"cl_decals") || !strcmp(t->cvar,"cl_fx3d_tex")) && t->values[choice] >= 2 &&
     !CL_FX4K_Allowed(t->cvar,t->values[choice])) return 0;	/* BDGN 3.56r: the 4K pack installed and S3TC */
 if (!strcmp(t->cvar,"cl_fx3d_tex") && t->values[choice] == 1 && !Cvar_VariableValue("bdgn_hdfx3d_sd_available") &&
     FS_LoadFile("models/hdfx3d_sd/puffs.tga",NULL) <= 0) return 0;	/* BDGN 3.56r: the row is also shown with only standard 4K */
 if (!strcmp(t->cvar,"gl_shadows") && choice==4 && !Cvar_VariableValue("bdgn_gl_volume_shadow_capable")) return 0;	/* 3.55ai: needs a stencil buffer */
 if (!strcmp(t->cvar,"gl_shadows") && choice>=2 && !Cvar_VariableValue("bdgn_gl_soft_shadow_capable")) return 0;	/* soft contact, from lights: modern drivers */
 if ((!strcmp(t->cvar,"cl_simpleenemy_anim") || !strcmp(t->cvar,"cl_simpleenemy_lod")) && choice && Cvar_VariableValue("r_duke_anim_support")<2) return 0;
 if ((!strcmp(t->cvar,"cl_weaponsprites") || !strcmp(t->cvar,"cl_weapon_simple")) && t->values[choice] &&
  Cvar_VariableValue("r_weaponsprite_support")<1) return 0;	/* BDGN 3.51q: ref_gl / ref_soft 3.51q with bdgn_weapons.pak */
 if (!strcmp(t->cvar,"cl_simpleenemies") && (choice == 2 || choice == 4) && !Cvar_VariableValue("r_simpleenemy_bright")) return 0;
 /* BDGN 3.51bct: the brightskins choices need a renderer with bright sprites (ref_gl) */
 if (!strcmp(t->cvar,"cl_classic2d") && t->values[choice] == 2 && !Cvar_VariableValue("r_simpleenemy_bright")) return 0;
 if (!strcmp(t->cvar,"cl_simple2denemies") && t->values[choice] >= 3 && !Cvar_VariableValue("r_simpleenemy_bright")) return 0;
 if ((t->flags & TK_GL) && !BDGN_RendererSupports(t->cvar)) return 0;
 if (t->values[choice]) {
  if ((t->flags & TK_3DNOW) && !Q3E_CPUHas3DNow()) return 0;
  if ((t->flags & TK_SSE) && !Q3E_CPUHasSSE()) return 0;
  if ((t->flags & TK_MMX) && !Q3E_CPUHasMMX()) return 0;
 }
 /* BDGN 3.51aut: with 3DNow! only what is fastest there: batching "auto (3DNow!)", no MMX (CL_3DNowEnforce) */
 if (Q3E_CPUHas3DNow()) {
  if ((!strcmp(t->cvar,"gl_fastdraw") || !strcmp(t->cvar,"gl_fastparticles")) && choice != 0) return 0;
  if ((!strcmp(t->cvar,"gl_mmx") || !strcmp(t->cvar,"s_mmx")) && t->values[choice]) return 0;
 }
 /* Keep the existing project policy: SSE renderer path can tint/darken textures. */
 if (!strcmp(t->cvar,"gl_sse") && t->values[choice]) return 0;
 if (!strcmp(t->cvar,"fastsqrt") && t->values[choice] == 2 && !Q3E_CPUHas3DNow()) return 0;
 return 1;
}
/* BDGN 3.58w (owner 2026-11-21): Doom mode and Wolfenstein 3D mode, two rows under classic 2d sprites mode, one cvar
   (cl_retromode: 0 off, 1 Doom, 2 Wolfenstein 3D, 3 Wolfenstein 3D low-res). Each mode is built on classic 2d sprites
   mode (its low-end sprites run underneath; the classic row shows "off" while a mode is on, and choosing it turns the
   mode off) and each step adds speed. MEASURED one at a time on the K6-3+ (Software 320x240, v3.58j): classic low-end
   +36.8 %, Doom light +7.8 % (OpenGL +6.7 %), half columns +8.7 %, light flash +7.4 %, simple explosions +2.5 %,
   locked view -1.8 % (kept: Doom's look and its autoaim, game/p_client.c, only on the own server / single player),
   flat floors +11.3 %, half rows +21.3 %, single-colour walls +34.7 %. The sums per mode are TEORETISK ANALYS until
   the benchmark cases RETRO / ... have run.
   - Doom: classic 2D sprites (3.59d: the full classic mode with its sprite explosions; was low-end with simple
     explosions), Doom light, half columns (Doom's low detail), locked view + autoaim, light flash; walls and floors
     keep their textures.
   - (3.59d) Wolfenstein 3D runs on the simplified (low-end) classic sprites with their sprite effects.
   - Wolfenstein 3D: as Doom, plus floors and ceilings in one colour (walls textured; OpenGL gl_flatfloors), no light
     (fullbright), Wolfenstein palette, view 80 %, half rows, no particles, PC speaker sound.
   - Wolfenstein 3D low-res: plus walls in their material colour (r_drawflat 2).
   Turning a mode off puts these back to their normal values and the sound back to what it was. */
static void BDGN_RetroExtrasOff (void)
{
 static const char *set[][2]={
  {"sw_doomlight","0"},{"gl_doomlight","0"},{"sw_halfcols","0"},{"cl_lockview","0"},{"cl_lightflash","0"},
  {"cl_static_rockets","0"},{"cl_static_bfg","0"},{"cl_simpleblaster","0"},{"gl_fastworld","0"},
  {"sw_flatfloors","0"},{"gl_flatfloors","0"},{"sw_wolfpalette","0"},{"viewsize","100"},{"sw_halfrows","0"},
  {"cl_particles","1"},{"r_drawflat","0"},{"r_spritetint","0"},{"cl_enemyskins","0"},
  {"cl_yshear","0"},{"sw_buildfloors","0"},{NULL,NULL}};	/* 3.60i: Duke Nukem 3D mode */
 int i;
 for (i=0;set[i][0];i++) Cvar_Set((char *)set[i][0],(char *)set[i][1]);
}
/* BDGN 3.59b (owner 2026-11-21): the texture packs follow the look: classic 2d sprites mode the low texture pack
   (fs_lowtextures 1), Doom and Wolfenstein 3D the ultra low pack (2). The value from before is kept in
   cl_retro_prevtex (-1: none kept) and comes back when the look is turned off. Presets set the packs themselves. */
static int s_tweak_texrestart;
static void BDGN_RetroTexturesOn (int pack)
{
 cvar_t *prev=Cvar_Get("cl_retro_prevtex","-1",CVAR_ARCHIVE);
 if (prev->value<0) Cvar_Set("cl_retro_prevtex",Cvar_VariableString("fs_lowtextures"));
 if (Cvar_VariableValue("fs_lowtextures")!=pack) { Cvar_SetValue("fs_lowtextures",pack); s_tweak_texrestart=1; }
}
static void BDGN_RetroTexturesOff (void)
{
 cvar_t *prev=Cvar_Get("cl_retro_prevtex","-1",CVAR_ARCHIVE);
 if (prev->value<0) return;
 if (Cvar_VariableValue("fs_lowtextures")!=prev->value) { Cvar_Set("fs_lowtextures",prev->string); s_tweak_texrestart=1; }
 Cvar_Set("cl_retro_prevtex","-1");
}
static int BDGN_ClassicRow (void)
{
 int i;
 for (i=0;i<bdgn_tweak_count;i++) if (!strcmp(bdgn_tweaks[i].cvar,"cl_classic2d")) return i;
 return -1;
}
void BDGN_SetClassicChoice (int index, int choice);
void BDGN_RetroModeSet (int mode)
{
 int prev=(int)Cvar_Get("cl_retromode","0",CVAR_ARCHIVE)->value, soft;
 int classic=BDGN_ClassicRow();
 if (mode<0 || mode>4) mode=0;
 soft=!Q_stricmp(Cvar_VariableString("vid_ref"),"soft") || !Q_stricmp(Cvar_VariableString("vid_ref"),"swam");
 if (!mode) {
  if (prev) {
   BDGN_RetroExtrasOff();
   if (classic>=0) BDGN_SetClassicChoice(classic,0);
   BDGN_RetroTexturesOff();	/* 3.59b */
   Cvar_Set("s_pcspeaker",Cvar_Get("cl_retromode_sound","0",CVAR_ARCHIVE)->string);
  }
  Cvar_SetValue("cl_retromode",0);
  return;
 }
 if (!prev) Cvar_Set("cl_retromode_sound",Cvar_Get("s_pcspeaker","0",CVAR_ARCHIVE)->string);
 BDGN_RetroExtrasOff();
 /* 3.59d (owner 2026-11-21): the explosions and hits are the sprite effects of classic 2d sprites mode, not the
    simple one-colour ones: Doom runs on classic 2d sprites mode itself, Wolfenstein 3D on its simplified (low-end)
    sprites */
 if (classic>=0) BDGN_SetClassicChoice(classic,mode==1||mode==4?1:3);
 Cvar_SetValue("cl_retromode",mode);
 Cvar_SetValue("sw_halfcols",1);
 Cvar_SetValue("cl_lockview",1);
 Cvar_SetValue("cl_lightflash",1);
 Cvar_SetValue("cl_static_rockets",0);	/* 0: the effects follow the 2D sprite mode */
 Cvar_SetValue("cl_static_bfg",0);
 Cvar_SetValue("cl_simpleblaster",0);
 /* 3.59b (owner): no simpler 3D models in the modes: the 2D enemy banks are found by the model's own skin, and a
    simpler model's skin has another name, so the enemies were drawn as simple 3D models instead of sprites */
 if (mode==4) {
  /* BDGN 3.60i (owner 2026-11-23): Duke Nukem 3D mode: the Build engine's look. Classic 2D sprites (as mode 1),
     the Duke Nukem 3D view (looking up / down moves the horizon, qcommon/yshear.h), Build floors (Software: straight
     steps, exact with the level view), light that fades with distance (Build's "visibility", as Doom light), the
     low texture pack; no locked view, no light flash, the sound as it was. Not a benchmark case (owner). */
  BDGN_RetroTexturesOn(1);
  Cvar_SetValue("cl_lockview",0);
  Cvar_SetValue("cl_lightflash",0);
  Cvar_SetValue("sw_halfcols",0);
  Cvar_SetValue("cl_yshear",1);
  Cvar_SetValue("sw_buildfloors",1);
  Cvar_SetValue(soft?"sw_doomlight":"gl_doomlight",1);
  Cvar_Set("s_pcspeaker",Cvar_VariableString("cl_retromode_sound"));
  return;
 }
 BDGN_RetroTexturesOn(FS_UltraTexPackAvailable()?2:1);	/* 3.59b (owner): Doom and Wolfenstein 3D: the ultra low textures */
 if (mode==1) {
  Cvar_SetValue(soft?"sw_doomlight":"gl_doomlight",1);
  Cvar_Set("s_pcspeaker",Cvar_VariableString("cl_retromode_sound"));
  return;
 }
 Cvar_SetValue("gl_fastworld",1);	/* no light at all (both renderers) */
 Cvar_SetValue("sw_flatfloors",1);
 Cvar_SetValue("gl_flatfloors",1);
 Cvar_SetValue("sw_wolfpalette",1);
 Cvar_SetValue("viewsize",80);
 Cvar_SetValue("sw_halfrows",1);
 Cvar_SetValue("cl_particles",0);
 Cvar_SetValue("s_nosound",0);
 Cvar_SetValue("s_midieffects",0);
 Cvar_SetValue("s_pcspeaker",1);	/* the speaker, or the sound card when there is none (win32/snd_pcspk.c) */
 S_StopAllSounds();
 Cvar_SetValue("r_spritetint",1);	/* the 2D enemies in one blue tone (Quake II's own sprites, tinted) */
 if (FS_LoadFile("sprites/anim32ww2/a041.dsa",NULL)>0) Cvar_SetValue("cl_enemyskins",1);	/* 3.58y: the soldiers in WWII uniform */
 if (mode==3) Cvar_SetValue("r_drawflat",2);	/* walls in their material colour */
}

int BDGN_TweakRead (int index)
{
 const bdgn_tweak_t *t = &bdgn_tweaks[index];
 float v = Cvar_Get ((char *)t->cvar, (char *)t->default_value, CVAR_ARCHIVE)->value;
 int i;
 if (!strcmp(t->cvar,"cl_classic2d") && Cvar_VariableValue("cl_retromode")) return 0;	/* BDGN 3.58w: a mode is on */
 if (!strcmp(t->cvar,"cl_classic2d") && v && Cvar_VariableValue("cl_lowend_sprites")) return 3;
 if (!strcmp(t->cvar,"cl_fxlevel")) {	/* BDGN 3.55ab: read from the rows it sets; 3.56r: level -> list position */
  int level=CL_FXLevel_Current();
  for (i=0;i<t->count;i++) if (t->values[i]==level) return i;
  return t->count-1;
 }
 if (t->kind && t->kind != 3) {
  if (v == 0) return Cvar_Get ((char *)t->aux,"1",CVAR_ARCHIVE)->value ? 0 : 1;
  return t->kind == 1 && v >= 1 && v <= 3 ? (int)v+1 : 2;
 }
 for (i=0;i<t->count;i++) if (v == t->values[i]) return i;
 return 0; /* Unlisted console values are preserved unless this row is changed. */
}
void BDGN_TweakSet (int index, int choice)
{
 const bdgn_tweak_t *t = &bdgn_tweaks[index];
 qboolean classic = !strcmp(t->cvar,"cl_classic2d");
 qboolean was_classic = classic && Cvar_VariableValue("cl_classic2d") != 0;
 if (!BDGN_TweakAllowed(index,choice)) return;
 if (!strcmp(t->cvar,"cl_fxlevel")) {	/* BDGN 3.55ab */
  CL_FXLevel_Apply(t->values[choice]);
  Cvar_SetValue("cl_fxlevel",(float)t->values[choice]);
  return;
 }
 if (!strcmp(t->cvar,"cl_retromode")) {	/* BDGN 3.58w */
  BDGN_RetroModeSet(t->values[choice]);
  return;
 }
 if (!strcmp(t->cvar,"cl_fastestpreset")) {	/* BDGN 3.59y */
  extern void VID_FastestPresetToggle (int on);
  VID_FastestPresetToggle(t->values[choice]);
  return;
 }
 if (t->aux) Cvar_SetValue ((char *)t->aux, t->kind == 3 ? t->values[choice] : choice == 0);
 if (classic && t->values[choice] && Cvar_VariableValue("cl_retromode"))
  BDGN_RetroModeSet(0);	/* BDGN 3.58w: choosing classic 2d sprites mode turns Doom / Wolfenstein 3D mode off */
 if (classic) {
  int mode=t->values[choice];
  Cvar_SetValue("cl_sprite_rate",BDGN_ClassicDefaultRate(mode));
  Cvar_SetValue("cl_lowend_sprites",mode==3);
  Cvar_SetValue("cl_classic2d",mode==3?1:mode);
  BDGN_ClassicSprites(mode==3?1:mode,was_classic);
  if (mode) BDGN_RetroTexturesOn(1); else BDGN_RetroTexturesOff();	/* 3.59b (owner): classic 2d: the low texture pack */
  return;
 }
 Cvar_SetValue ((char *)t->cvar,t->values[choice]);
 if(!strcmp(t->cvar,"cl_static_rockets")) Cvar_SetValue("cl_simpleblaster",t->values[choice]);
 if(!strcmp(t->cvar,"cl_simple2denemies")) BDGN_EnemyLook();	/* BDGN 3.51y */
 if(!strcmp(t->cvar,"cl_sprite_rate")) BDGN_SpriteRate();	/* BDGN 3.51z */	/* BDGN 3.51w: blaster bolts and hits follow simple explosions */
 if(!strcmp(t->cvar,"cl_simpleenemy_anim") && t->values[choice]) Cvar_SetValue("cl_staticenemies",0);
 if(!strcmp(t->cvar,"cl_staticenemies") && t->values[choice]) Cvar_SetValue("cl_simpleenemy_anim",0);
 if(!strcmp(t->cvar,"cl_staticenemies") && !t->values[choice] && Cvar_VariableValue("cl_classic2d") && Cvar_VariableValue("r_duke_anim_support")>=2) Cvar_SetValue("cl_simpleenemy_anim",BDGN_RateValue());	/* BDGN 3.51x: unfreeze under classic 2d sprites mode */
}
/* BDGN 3.58w: classic 2d sprites mode set by Doom / Wolfenstein 3D mode (the same steps as its menu row) */
void BDGN_SetClassicChoice (int index, int choice)
{
 const bdgn_tweak_t *t=&bdgn_tweaks[index];
 int mode=t->values[choice];
 qboolean was_classic=Cvar_VariableValue("cl_classic2d")!=0;
 if (!BDGN_TweakAllowed(index,choice)) return;
 Cvar_SetValue("cl_sprite_rate",BDGN_ClassicDefaultRate(mode));
 Cvar_SetValue("cl_lowend_sprites",mode==3);
 Cvar_SetValue("cl_classic2d",mode==3?1:mode);
 BDGN_ClassicSprites(mode==3?1:mode,was_classic);
}
/* BDGN 3.51z: short names (screens under 640 wide) looked up by cvar, so rows can be moved in the table */
static const char *s_tweak_short_pairs[][2] = { {"gl_fastdraw","model batching"}, {"gl_fastparticles","particle batching"}, {"gl_batchworld","world batching"}, {"gl_finish","frame sync"}, {"gl_ext_multitexture","multitexture"}, {"gl_fastworld","fullbright"}, {"gl_shadows","shadows"}, {"gl_dynamic","dynamic lights"}, {"gl_dlight_cull","skip unseen lights"}, {"gl_dlight_maxdist","light distance"}, {"gl_flatmodels","flat model light"}, {"cl_staticlightstyles","static lightstyles"}, {"r_drawflat","single colours"}, {"gl_fastsky","simple sky"}, {"gl_fastwater","simple water"}, {"cl_staticmap","map / pickup anim"}, {"gl_lowdetail","far model detail"}, {"gl_maxtexsize","max texture size"}, {"cl_fovaspect","widescreen FOV"}, {"fastsqrt","fast sqrt"}, {"fastsqrt2","sqrt move/game"}, {"cl_classic2d","classic 2d sprites"}, {"cl_simple2denemies","static 2d enemies"}, {"cl_sprite_rate","2d enemy rate"}, {"cl_weaponsprites","weapon look"}, {"cl_simpleenemy_lod","far enemy anim"}, {"cl_staticenemies","freeze 3D enemies"}, {"cl_bloodfx","blood / bullet hits"}, {"cl_sprite_hits","sprite hits (test)"}, {"cl_static_rockets","simple explosions"}, {"cl_notrails","remove trails"}, {"cl_sprite_trails","sprite trails (test)"}, {"cl_simple_rail","simple railgun"}, {"cl_nosteam","steam / smoke / flies"}, {"g_gibmode","remove gibs / debris"}, {"cl_nosmoke","remove smoke"}, {"cl_nobanners","hide banners"}, {"cl_nodecorpses","hide corpses"}, {"cl_nospawnpads","hide spawn pads"}, {"s_basic_sounds","basic sounds"}, {"cl_weapon_simple","static sprite weapon"} };
static const char *TweakShortName (const bdgn_tweak_t *t)
{
 int i;
 for (i = 0; i < (int)(sizeof(s_tweak_short_pairs)/sizeof(s_tweak_short_pairs[0])); i++)
  if (!strcmp (s_tweak_short_pairs[i][0], t->cvar)) return s_tweak_short_pairs[i][1];
 return t->label;
}
#define TWEAK_PAGES 8	/* BDGN 3.57d: 6 simpler graphics, 7 old-school modes */
static menuframework_s s_tweak_menu[TWEAK_PAGES];
static menulist_s s_tweak_rows[TK_MAX];
static menuseparator_s s_tweak_heads[TWEAK_PAGES][8];
static menuaction_s s_tweak_apply[TWEAK_PAGES], s_tweak_bench[TWEAK_PAGES];
static int s_tweak_page, s_tweak_scroll[TWEAK_PAGES], s_tweak_changed[TK_MAX];
static menuaction_s s_tweak_enemy_link, s_tweak_paths_link;	/* 3.57h: paths link on page 7 */
static menuslider_s s_explosion_debris_slider;
static char s_explosion_debris_status[64];
static void ExplosionDebrisStatus(void)
{
 int count=(int)s_explosion_debris_slider.curvalue;
 Com_sprintf(s_explosion_debris_status,sizeof(s_explosion_debris_status),
             "%d / %d pieces per 3D explosion",count,EXPLOSION_DEBRIS_MAX);
}
static void ExplosionDebrisCallback(void *unused)
{
 Cvar_SetValue("cl_explosion_debris",s_explosion_debris_slider.curvalue);
 ExplosionDebrisStatus();
}
static menuslider_s s_relief_slider;	/* BDGN 3.55aj: depth of the 3D surface structure (gl_relief_depth 0.5 .. 4) */
static void ReliefDepthCallback (void *unused) { Cvar_SetValue("gl_relief_depth",s_relief_slider.curvalue*0.25f); }
static void TweaksOpen(int page);
static void TweaksEnemyMenu(void *unused) { TweaksOpen(2); }

/* BDGN 3.57d: a short note to the right of each row that changes the picture: what the setting does to how the game
   looks. Rows that only change speed (draw paths, maths, sound, assembly) have none. */
static const char *s_tweak_notes[][2] = {
 /* better looking */
 {"gl_shadows","shadows under models"}, {"gl_pixel_lighting","smooth per-pixel light"}, {"gl_pixel_bump","bumpy wall surfaces"},
 {"gl_sun","sun and lamp shadows"}, {"gl_godrays","beams of light"},
 {"cl_fxlevel","quality of all effects"}, {"cl_hdfx","sharper effect sprites"}, {"cl_decals","bullet holes, scorch"},
 {"cl_3dexplo","3D fireballs"}, {"cl_3dbfg","3D BFG ball"}, {"cl_3dblaster","3D blaster bolts"}, {"cl_3drail","3D railgun spiral"},
 {"cl_3drail_colors","railgun colours"}, {"cl_hdgibs","3D gibs and blood"}, {"cl_hdsmoke","3D smoke puffs"},
 {"cl_lavasmoke","smoke over lava"}, {"cl_lavabursts","lava splashes"}, {"cl_cells","cells eject from guns"},
 {"cl_hdmuzzle","3D muzzle flashes"}, {"cl_shells","shells fly out"}, {"cl_heatdistort","hot air shimmer"},
 {"cl_fx3d_tex","texture set of effects"}, {"gl_relief","3D wall structure"}, {"gl_bloom","glow around lights"},
 {"gl_ssao","shade in corners"}, {"gl_tonemap","film-like colours"}, {"gl_colour_grade","warmer / cooler tint"},
 {"gl_water_surface","waving water"}, {"gl_water_interaction","ripples and splashes"}, {"gl_water_reflect","mirror on water"},
 /* simpler graphics */
 {"gl_fastworld","no light, all bright"}, {"gl_dynamic","no muzzle/blast light"}, {"gl_dlight_maxdist","far lights not shown"},
 {"gl_flatmodels","models lit evenly"}, {"cl_staticlightstyles","no flickering lights"}, {"r_drawflat","walls in one colour"},
 {"gl_fastsky","sky in one colour"}, {"gl_fastwater","water does not wave"}, {"gl_lowdetail","far models move jerky"},
 {"gl_maxtexsize","blurrier textures"}, {"gl_picmip","blurrier walls, less card memory"},
 {"gl_playermip","blurrier players, less memory"}, {"r_lerpmodels","off: models move in steps"},
 {"cl_gun","hidden: weapon not drawn"}, {"cl_bloodfx","less blood and sparks"}, {"cl_static_rockets","simpler explosions"},
 {"cl_simple_rail","plain railgun beam"}, {"cl_notrails","no rocket trails"}, {"cl_nosmoke","no smoke or flashes"},
 {"cl_nosteam","no steam and flies"}, {"g_gibmode","fewer body parts"}, {"cl_nobanners","no banners"},
 {"cl_nodecorpses","no decorative bodies"}, {"cl_nospawnpads","no spawn pads"},
 /* 2D enemies and sprites */
 {"cl_classic2d","enemies as 2D sprites"}, {"cl_enemyskins","soldiers in field grey"}, {"cl_retroweapons","coarse Doom / Wolf3D pixels"}, {"r_spritetint","2D enemies in one blue"}, {"gl_sse_packed","4 texels per SSE step"}, {"cpu_profile","tuned for this processor"}, {"cl_retromode","faster each step, classic off"}, {"cl_sprite_rate","choppier animation"}, {"cl_weapon_simple","weapon stands still"},
 {"cl_duke_directions","fewer sprite angles"}, {"cl_simpleenemy_lod","far enemies choppy"}, {"cl_staticenemies","enemies do not move"},
 {"cl_simple2denemies","enemies flat pictures"}, {"r_brightskins","enemies glow bright"}, {"cl_staticmap","items do not rotate"},
 {"cl_spritelight","sprites lit by the map"}, {"cl_fovaspect","wider view on 16:9"},
 /* old-school modes */
 {"sw_doomlight","one light per surface"}, {"gl_doomlight","one light per surface"}, {"sw_blakelight","no light maps"},
 {"gl_blakelight","no light maps"}, {"cl_lightflash","screen flash, no light"}, {"sw_q1fog","fades to black"},
 {"gl_q1fog","fades to black"}, {"sw_halfrows","chunky rows"}, {"sw_halfcols","chunky columns"},
 {"sw_flatfloors","flat-coloured surfaces"}, {"sw_buildfloors","floors bend up/down"},
 {"sw_stipplealpha","glass and water solid"}, {"gl_solidalpha","glass and water solid"}, {"sw_q1particles","square particles"},
 {"gl_q1particles","square particles"}, {"sw_wolfpalette","only 125 colours"}, {"cl_lockview","cannot look up/down"},
 {"viewsize","smaller picture"}, {"cl_far2d","far enemies in 2D"}, {"cl_objdist","far cosmetic debris hidden"},
 /* 3.60c: rows that were on the TEST Low End page */
 {"cl_grenadesmoke","smoke behind grenades"}, {"sw_upscale","same picture, faster 75 %"}, {"cl_explosmoke","puff after explosion"},	/* 3.57h */
 {"cl_simplemodels","fewer triangles, blurrier skins"},	/* 3.57l */
 {"cl_simplemodels_far","far models cruder"}, {"cl_modellod","far: cruder, then 2D"}, {"cl_simpleworld","less wall/ceiling detail"}, {"cl_lowspec3d","all three below at once"}, {"s_mono","sound from the middle"},	/* 3.57m */
 {"sw_render_scale","blurrier picture"}, {"sw_test_gray_lightmaps","grey light"}, {"sw_test_compact_walls","blurrier walls"},
 {"sw_drawdist","fog, shorter view"}, {"s_khz","sound quality"},
 {NULL,NULL}
};
static const char *TweakNoteFor (const char *cvar)
{
 int i;
 for (i=0;s_tweak_notes[i][0];i++) if (!strcmp(s_tweak_notes[i][0],cvar)) return s_tweak_notes[i][1];
 return NULL;
}
static const char *TweakNote (void *item)
{
 menucommon_s *c=(menucommon_s *)item;
 if (c->type==MTYPE_SLIDER) {	/* the two sliders under their switches */
  if (item==(void *)&s_explosion_debris_slider) return "pieces per explosion";
  if (item==(void *)&s_relief_slider) return "depth of the structure";
  return NULL;
 }
 return TweakNoteFor(bdgn_tweaks[c->localdata[0]].cvar);
}
static void TweaksApply (void *unused)
{
 int i, restart = 0, sound_restart = 0;
 for (i=0;i<bdgn_tweak_count;i++) if (s_tweak_changed[i]) {
  if ((bdgn_tweaks[i].flags & TK_RESTART) && Cvar_VariableValue((char *)bdgn_tweaks[i].cvar) != bdgn_tweaks[i].values[s_tweak_rows[i].curvalue]) restart = 1;
  if (!strcmp(bdgn_tweaks[i].cvar,"s_khz")) {	/* 3.60c: the row moved from page 3 to the advanced page */
   static const int profiles[]={4,0,1,2,3};
   M_SetSoundQuality(profiles[s_tweak_rows[i].curvalue]);
   sound_restart=1;
  }
  if (!strcmp(bdgn_tweaks[i].cvar,"s_mono") && Cvar_VariableValue("s_mono") != bdgn_tweaks[i].values[s_tweak_rows[i].curvalue])
   sound_restart=1;	/* BDGN 3.57m */
  if (!strcmp(bdgn_tweaks[i].cvar,"cl_fastestpreset")) {
   /* A complete graphics/audio profile is applied, even if the final ON value
      matches the old one after several arrow presses. Reopen both backends. */
   restart=1;
   sound_restart=1;
  }
  BDGN_TweakSet(i,s_tweak_rows[i].curvalue);
  s_tweak_changed[i]=0;
 }
 CL_CheckFullbright();
 CL_ModelLodSync ();	/* 3.57t: cl_lowspec3d / cl_modellod set their rows now, before the renderer restarts */
 if (s_tweak_texrestart) { restart = 1; s_tweak_texrestart = 0; }	/* 3.59b: a texture pack changed with the look */
 if (restart) Cbuf_AddText ("vid_restart\n");
 if (sound_restart) Cbuf_AddText("snd_restart\n");
 M_PopMenu();
}
static menuframework_s s_benchmark_menu;
static qboolean s_benchmark_from_tweaks;
/* BDGN 3.60c (owner, 2026-11-22: the release keeps only the benchmark that matters to players): "Quick: my presets",
   every preset on the renderer and driver in use, 3 runs each (bdgn_bench_presets). The other benchmarks are gone. */
static char s_benchmark_label[48];
static menuaction_s s_benchmark_action;

static void BenchmarkStart (void *item)
{
 if(((menuaction_s *)item)->generic.flags&QMF_GRAYED) return;
 if(s_benchmark_from_tweaks) {
  M_PopMenu();
  TweaksApply(NULL);
 }
 M_ForceMenuOff();
 Cbuf_AddText("bdgn_bench_presets\n");
}

static void BenchmarkDraw (void)
{
 M_Banner("m_banner_video");
 Menu_AdjustCursor(&s_benchmark_menu,1);
 Menu_Draw(&s_benchmark_menu);
}

static const char *BenchmarkKey (int key)
{
 return Default_MenuKey(&s_benchmark_menu,key);
}

static void BenchmarkOpen (qboolean from_tweaks)
{
 qboolean estimated;
 int runs=CL_BenchmarkMenuRuns(BM_ALL_QUICK,"presets",&estimated);
 s_benchmark_from_tweaks=from_tweaks;
 memset(&s_benchmark_menu,0,sizeof(s_benchmark_menu));
 s_benchmark_menu.x=viddef.width/2-104;
 s_benchmark_menu.y=viddef.height/2-55;
 s_benchmark_menu.statusbar="the presets on your renderer; the log is written to baseq2";
 memset(&s_benchmark_action,0,sizeof(s_benchmark_action));
 if(runs>0) Com_sprintf(s_benchmark_label,sizeof(s_benchmark_label),"Quick: my presets (%s%i runs)",estimated?"~":"",runs);
 else Com_sprintf(s_benchmark_label,sizeof(s_benchmark_label),"Quick: my presets (%s)",runs==0?"unavailable":"busy");
 s_benchmark_action.generic.type=MTYPE_ACTION;
 s_benchmark_action.generic.name=s_benchmark_label;
 s_benchmark_action.generic.flags=QMF_LEFT_JUSTIFY | (runs<=0?QMF_GRAYED:0);
 s_benchmark_action.generic.callback=BenchmarkStart;
 Menu_AddItem(&s_benchmark_menu,&s_benchmark_action);
 M_PushMenu(BenchmarkDraw,BenchmarkKey);
}

void M_Menu_Benchmarks_f (void) { BenchmarkOpen(false); }
static void TweaksBenchmark (void *unused) { BenchmarkOpen(true); }

static void TweaksPendingRules (void)
{
 int i, enemy_mode=0, classic=0, flat = Cvar_VariableValue("gl_hack_notextures") != 0;
 for(i=0;i<bdgn_tweak_count;i++)if(!strcmp(bdgn_tweaks[i].cvar,"cl_simpleenemies"))enemy_mode=bdgn_tweaks[i].values[bdgn_tweaks[i].menu==s_tweak_page?s_tweak_rows[i].curvalue:BDGN_TweakRead(i)];
 for (i=0;i<bdgn_tweak_count;i++) if (!strcmp(bdgn_tweaks[i].cvar,"r_drawflat"))	/* BDGN 3.55i: on another page = its current value */
  flat |= bdgn_tweaks[i].values[bdgn_tweaks[i].menu==s_tweak_page?s_tweak_rows[i].curvalue:BDGN_TweakRead(i)] != 0;
 for (i=0;i<bdgn_tweak_count;i++) if (!strcmp(bdgn_tweaks[i].cvar,"cl_classic2d") && bdgn_tweaks[i].menu == s_tweak_page) classic = s_tweak_rows[i].curvalue;
 for (i=0;i<bdgn_tweak_count;i++) if (bdgn_tweaks[i].menu == s_tweak_page) {
  s_tweak_rows[i].generic.flags = ((bdgn_tweaks[i].flags & TK_GL) && !BDGN_RendererSupports(bdgn_tweaks[i].cvar)) ? QMF_GRAYED : 0;
  if(!strcmp(bdgn_tweaks[i].cvar,"cl_duke_directions") &&
     (!classic || Cvar_VariableValue("r_duke_anim_support")<2 || Cvar_VariableValue("bdgn_extra_lowram")))
   s_tweak_rows[i].generic.flags|=QMF_GRAYED;
  if((!strcmp(bdgn_tweaks[i].cvar,"cl_simpleenemy_anim") || !strcmp(bdgn_tweaks[i].cvar,"cl_simpleenemy_lod")) && (enemy_mode<3 || Cvar_VariableValue("r_duke_anim_support")<2))s_tweak_rows[i].generic.flags|=QMF_GRAYED;
  if (!strcmp(bdgn_tweaks[i].cvar,"cl_weapon_simple") &&
      (classic==3 || Cvar_VariableValue("r_weaponsprite_support")<1 || (!classic && !Cvar_VariableValue("cl_weaponsprites")))) {
   s_tweak_rows[i].generic.flags |= QMF_GRAYED;
   if(Cvar_VariableValue("r_weaponsprite_support")<1) s_tweak_rows[i].curvalue=0;
  }	/* BDGN 3.51ab: only with the 2D weapon (classic 2d sprites mode) */
  if (!strcmp(bdgn_tweaks[i].cvar,"gl_fastworld") && flat) {
   if (s_tweak_rows[i].curvalue) { s_tweak_rows[i].curvalue = 0; s_tweak_changed[i] = 1; }
   s_tweak_rows[i].generic.flags |= QMF_GRAYED;
  }
 }
}
static void TweaksDraw (void)
{
 menuframework_s *m = &s_tweak_menu[s_tweak_page], visible;
 menucommon_s *item;
 int i, top = viddef.height/2-60, height = viddef.height-top-32, first=-1;
 M_Banner ("m_banner_video");
 Menu_AdjustCursor(m,1);
 TweaksPendingRules();
 item = (menucommon_s *)Menu_ItemAtCursor(m);
 if (item->y < s_tweak_scroll[s_tweak_page]) s_tweak_scroll[s_tweak_page] = item->y;
 if (item->y > s_tweak_scroll[s_tweak_page]+height-10) s_tweak_scroll[s_tweak_page] = item->y-height+10;
 m->y = top-s_tweak_scroll[s_tweak_page];
 visible = *m; visible.nitems=0;
 for (i=0;i<m->nitems;i++) {
  item = (menucommon_s *)m->items[i];
  if (item->y >= s_tweak_scroll[s_tweak_page] && item->y <= s_tweak_scroll[s_tweak_page]+height-10) {
   if (first < 0) first=i;
   visible.items[visible.nitems++] = item;
  }
 }
 visible.cursor = m->cursor-first;
 item=(menucommon_s *)Menu_ItemAtCursor(m);
 if(item->type==MTYPE_SPINCONTROL) {
  int index=item->localdata[0];
  if(bdgn_tweaks[index].kind==3 && Cvar_VariableValue((char *)bdgn_tweaks[index].cvar)!=Cvar_VariableValue((char *)bdgn_tweaks[index].aux))visible.statusbar="Mixed: choose a shared mode";
 }
 Menu_Draw(&visible);
}
static const char *TweaksKey (int key)
{
 menuframework_s *m=&s_tweak_menu[s_tweak_page];
 menucommon_s *item=(menucommon_s *)Menu_ItemAtCursor(m);
 int direction, index, choice;
 if (key == K_ESCAPE) {	/* BDGN 3.60f: leaving a page with changes applies them, as Video does (before: lost without a word) */
  int n, changed=0;
  for(n=0;n<bdgn_tweak_count;n++) if(bdgn_tweaks[n].menu==s_tweak_page && s_tweak_changed[n]) changed=1;
  if (changed) TweaksApply(NULL); else M_PopMenu();
  return menu_out_sound;
 }
 if (key == K_ENTER || key == K_KP_ENTER) {
  if (item == (menucommon_s *)&s_tweak_enemy_link) TweaksEnemyMenu(NULL);
  else if (item == (menucommon_s *)&s_tweak_paths_link) TweaksOpen(4);	/* BDGN 3.57h */
  else if (item == (menucommon_s *)&s_tweak_bench[s_tweak_page]) TweaksBenchmark(NULL);
  else TweaksApply(NULL);
  return menu_move_sound;
 }
 if (key == K_LEFTARROW || key == K_KP_LEFTARROW || key == K_RIGHTARROW || key == K_KP_RIGHTARROW) {
  if (item->type == MTYPE_SLIDER) {	/* BDGN 3.55aj */
   Menu_SlideItem(m,(key == K_LEFTARROW || key == K_KP_LEFTARROW) ? -1 : 1);
   return menu_move_sound;
  }
  if (item->type != MTYPE_SPINCONTROL || (item->flags & QMF_GRAYED)) return menu_move_sound;
  direction = (key == K_LEFTARROW || key == K_KP_LEFTARROW) ? -1 : 1;
  index=item->localdata[0]; choice=s_tweak_rows[index].curvalue+direction;
  if (s_tweak_page!=3 && !strcmp(bdgn_tweaks[index].cvar,"g_gibmode") && choice == 0) return menu_move_sound;
  while (choice >= 0 && choice < bdgn_tweaks[index].count && !BDGN_TweakAllowed(index,choice)) choice+=direction;
  if (choice >= 0 && choice < bdgn_tweaks[index].count) {
   int n, previous=s_tweak_rows[index].curvalue;
   s_tweak_rows[index].curvalue=choice; s_tweak_changed[index]=1;
   /* BDGN 3.59b (owner 2026-11-21): classic 2d sprites mode, Doom mode and Wolfenstein 3D mode are one choice:
      turning one on shows the other two as off at once (and only the one chosen is applied) */
   if (bdgn_tweaks[index].values[choice] &&
       (!strcmp(bdgn_tweaks[index].cvar,"cl_classic2d") || !strcmp(bdgn_tweaks[index].cvar,"cl_retromode")))
    for (n=0;n<bdgn_tweak_count;n++)
     if (n!=index && bdgn_tweaks[n].menu==s_tweak_page &&
         (!strcmp(bdgn_tweaks[n].cvar,"cl_classic2d") || !strcmp(bdgn_tweaks[n].cvar,"cl_retromode")))
      { s_tweak_rows[n].curvalue=0; s_tweak_changed[n]=0; }
   if (!strcmp(bdgn_tweaks[index].cvar,"cl_classic2d")) {
    for (n=0;n<bdgn_tweak_count;n++) {
     if (!previous && choice && !strcmp(bdgn_tweaks[n].cvar,"cl_static_rockets") && bdgn_tweaks[n].menu>=0 && bdgn_tweaks[n].menu!=s_tweak_page) {
      s_tweak_rows[n].curvalue=0;	/* BDGN 3.55i: Simple Explosions is on the effects page now; Classic 2D still turns it off */
      s_tweak_changed[n]=1;
     }
     if (bdgn_tweaks[n].menu!=s_tweak_page) continue;
     if (!strcmp(bdgn_tweaks[n].cvar,"cl_sprite_rate")) {
      s_tweak_rows[n].curvalue=BDGN_ClassicDefaultRate(bdgn_tweaks[index].values[choice])-1;
      s_tweak_changed[n]=1;
     }
     if (!strcmp(bdgn_tweaks[n].cvar,"cl_weaponsprites") && BDGN_TweakAllowed(n,choice!=0)) {
      s_tweak_rows[n].curvalue=choice!=0;
      s_tweak_changed[n]=1;
     }
     if (!previous && choice && !strcmp(bdgn_tweaks[n].cvar,"cl_static_rockets")) {
      s_tweak_rows[n].curvalue=0;
      s_tweak_changed[n]=1;
     }
     if (choice && (!strcmp(bdgn_tweaks[n].cvar,"cl_simple2denemies") || !strcmp(bdgn_tweaks[n].cvar,"cl_staticenemies"))) {
      s_tweak_rows[n].curvalue=0;
      s_tweak_changed[n]=1;
     }
    }
   }
   if(bdgn_tweaks[index].values[choice] && (!strcmp(bdgn_tweaks[index].cvar,"cl_simpleenemy_anim") || !strcmp(bdgn_tweaks[index].cvar,"cl_staticenemies")))
    for(n=0;n<bdgn_tweak_count;n++)if(!strcmp(bdgn_tweaks[n].cvar,!strcmp(bdgn_tweaks[index].cvar,"cl_simpleenemy_anim")?"cl_staticenemies":"cl_simpleenemy_anim")) {s_tweak_rows[n].curvalue=0;s_tweak_changed[n]=1;}
   TweaksPendingRules();
  }
  return menu_move_sound;
 }
 return Default_MenuKey(m,key);
}
static void TweaksGraphicsDraw(void){s_tweak_page=0;TweaksDraw();}
static void TweaksEffectsDraw(void){s_tweak_page=1;TweaksDraw();}
static void TweaksEnemyDraw(void){s_tweak_page=2;TweaksDraw();}
static void TweaksPathsDraw(void){s_tweak_page=4;TweaksDraw();}
static const char *TweaksGraphicsKey(int key){s_tweak_page=0;return TweaksKey(key);}
static const char *TweaksEffectsKey(int key){s_tweak_page=1;return TweaksKey(key);}
static const char *TweaksEnemyKey(int key){s_tweak_page=2;return TweaksKey(key);}
static const char *TweaksPathsKey(int key){s_tweak_page=4;return TweaksKey(key); }
static void TweaksTestingDraw(void){s_tweak_page=5;TweaksDraw();}	/* BDGN 3.55ai */
static const char *TweaksTestingKey(int key){s_tweak_page=5;return TweaksKey(key); }
static void TweaksSimplerDraw(void){s_tweak_page=6;TweaksDraw();}	/* BDGN 3.57d */
static const char *TweaksSimplerKey(int key){s_tweak_page=6;return TweaksKey(key); }
static void TweaksOldDraw(void){s_tweak_page=7;TweaksDraw();}
static const char *TweaksOldKey(int key){s_tweak_page=7;return TweaksKey(key); }
static void TweaksOpen (int page)
{
 menuframework_s *m=&s_tweak_menu[page];
 const char *category="";
 int i, y=0, heading=0;
 CL_CheckFullbright(); s_tweak_page=page; s_tweak_scroll[page]=0;
 memset(m,0,sizeof(*m));
 for(i=0;i<bdgn_tweak_count;i++)if(bdgn_tweaks[i].menu==page)s_tweak_changed[i]=0;	/* BDGN 3.55i: every page on its own */
 m->x=viddef.width/2; m->statusbar=viddef.width < 640 ? "Enter: apply   Esc: cancel" : "Arrows: choose   Enter/Apply: save   Esc: cancel";
 for (i=0;i<bdgn_tweak_count;i++) {
  const bdgn_tweak_t *t=&bdgn_tweaks[i];
  /* BDGN 3.51x: the "simple enemies..." page link was replaced by classic 2d sprites mode (the page's settings are
     console cvars now) */
  if(bdgn_tweaks[i].menu!=page)continue;
  if(!BDGN_TweakVisible(i))continue;	/* BDGN 3.55i: unsupported rows are hidden */
  if (strcmp(category,t->category)) {
   menuseparator_s *head=&s_tweak_heads[page][heading++];
   memset(head,0,sizeof(*head)); head->generic.type=MTYPE_SEPARATOR;
   if ((page == 1 || page >= 6) && category[0]) y += 10;	/* 3.57d: also the two new pages */
   head->generic.name=t->category; head->generic.y=y; Menu_AddItem(m,head); y+=14; category=t->category;
  }
  memset(&s_tweak_rows[i],0,sizeof(s_tweak_rows[i]));
  s_tweak_rows[i].generic.type=MTYPE_SPINCONTROL; s_tweak_rows[i].generic.name=viddef.width < 640 ? TweakShortName(t) : t->label;
  if (viddef.width < 640 && !strcmp(t->cvar,"cl_simpleenemies")) s_tweak_rows[i].generic.x=-16;
  s_tweak_rows[i].generic.y=y; s_tweak_rows[i].generic.localdata[0]=i;
  s_tweak_rows[i].itemnames=(const char **)t->names; s_tweak_rows[i].curvalue=BDGN_TweakRead(i);
  if (!strcmp(t->cvar,"gl_shadows") && !Cvar_VariableValue("bdgn_gl_soft_shadow_capable")) {
   static const char *legacy_shadows[]={"off","original",NULL};
   s_tweak_rows[i].itemnames=legacy_shadows;
   if (s_tweak_rows[i].curvalue>=2) s_tweak_rows[i].curvalue=0;
  } else if (!strcmp(t->cvar,"gl_shadows") && !Cvar_VariableValue("bdgn_gl_volume_shadow_capable")) {
   static const char *nostencil_shadows[]={"off","original","soft contact","from lights",NULL};	/* 3.55ai */
   s_tweak_rows[i].itemnames=nostencil_shadows;
   if (s_tweak_rows[i].curvalue>=4) s_tweak_rows[i].curvalue=3;
  }
  Menu_AddItem(m,&s_tweak_rows[i]); y+=10;
  if (!strcmp(t->cvar,"cl_3dexplo"))
  {
   memset(&s_explosion_debris_slider,0,sizeof(s_explosion_debris_slider));
   s_explosion_debris_slider.generic.type=MTYPE_SLIDER;
   s_explosion_debris_slider.generic.name=viddef.width < 640 ? "debris" : "explosion debris";
   s_explosion_debris_slider.generic.y=y;
   s_explosion_debris_slider.generic.callback=ExplosionDebrisCallback;
   s_explosion_debris_slider.generic.statusbar=s_explosion_debris_status;
   s_explosion_debris_slider.maxvalue=EXPLOSION_DEBRIS_MAX;
   s_explosion_debris_slider.curvalue=(float)CL_Gore_DebrisCount();
   ExplosionDebrisStatus();
   Menu_AddItem(m,&s_explosion_debris_slider); y+=10;
  }
  if (!strcmp(t->cvar,"gl_relief")) {	/* BDGN 3.55aj: the depth slider under its switch; works at once, no apply needed */
   float depth=Cvar_VariableValue("gl_relief_depth");
   if (depth<0.5f) depth=1.5f;
   if (depth>4) depth=4;
   memset(&s_relief_slider,0,sizeof(s_relief_slider));
   s_relief_slider.generic.type=MTYPE_SLIDER;
   s_relief_slider.generic.name=viddef.width < 640 ? "depth" : "structure depth";
   s_relief_slider.generic.y=y;
   s_relief_slider.generic.callback=ReliefDepthCallback;
   s_relief_slider.minvalue=2; s_relief_slider.maxvalue=16;
   s_relief_slider.curvalue=(float)(int)(depth*4+0.5f);
   Menu_AddItem(m,&s_relief_slider); y+=10;
  }
 }
 {	/* BDGN 3.57d: room for the notes: labels, values and notes side by side, centred when they fit */
  int maxlab=0, maxval=0, maxnote=0, k, n, total, w=viddef.width;
  for (k=0;k<m->nitems;k++) {
   menucommon_s *c=(menucommon_s *)m->items[k];
   const char *note;
   if (c->type!=MTYPE_SPINCONTROL && c->type!=MTYPE_SLIDER) continue;
   if (c->name && (int)strlen(c->name)>maxlab) maxlab=strlen(c->name);
   if (c->type==MTYPE_SLIDER) { if (maxval<12) maxval=12; }
   else {
    const char **names=((menulist_s *)c)->itemnames;
    for (n=0;names && names[n];n++) if ((int)strlen(names[n])>maxval) maxval=strlen(names[n]);
   }
   note=TweakNote(c);
   if (note && (int)strlen(note)>maxnote) maxnote=strlen(note);
  }
  m->note=NULL; m->notecolumn=0; m->notechars=0;
  if (maxnote) {
   total=16+maxlab*8+16+maxval*8+16+maxnote*8;
   if (total<=w-16) m->x=8+16+maxlab*8+(w-16-total)/2;
   else if (24+maxlab*8<m->x) m->x=24+maxlab*8;
   m->note=TweakNote;
   m->notecolumn=16+maxval*8+16;
   m->notechars=(w-8-(m->x+m->notecolumn))/8;
  }
 }
 if (0) {	/* BDGN 3.57h: draw paths & detail was a link on the old-school page; 3.57v: a row on the Video page instead */
  memset(&s_tweak_paths_link,0,sizeof(s_tweak_paths_link));
  s_tweak_paths_link.generic.type=MTYPE_ACTION;
  s_tweak_paths_link.generic.name="draw paths & detail...";
  s_tweak_paths_link.generic.flags=QMF_LEFT_JUSTIFY;
  s_tweak_paths_link.generic.x=-80;
  s_tweak_paths_link.generic.y=y;
  Menu_AddItem(m,&s_tweak_paths_link);
  y+=14;
 }
 memset(&s_tweak_apply[page],0,sizeof(s_tweak_apply[page]));
 s_tweak_apply[page].generic.type=MTYPE_ACTION; s_tweak_apply[page].generic.name="apply";
 s_tweak_apply[page].generic.flags=QMF_LEFT_JUSTIFY; s_tweak_apply[page].generic.x=-80;
 s_tweak_apply[page].generic.y=y+8; s_tweak_apply[page].generic.callback=TweaksApply; Menu_AddItem(m,&s_tweak_apply[page]);
 memset(&s_tweak_bench[page],0,sizeof(s_tweak_bench[page]));
 s_tweak_bench[page].generic.type=MTYPE_ACTION; s_tweak_bench[page].generic.name="benchmarks...";
 s_tweak_bench[page].generic.flags=QMF_LEFT_JUSTIFY; s_tweak_bench[page].generic.x=-80;
 s_tweak_bench[page].generic.y=y+22; s_tweak_bench[page].generic.callback=TweaksBenchmark; Menu_AddItem(m,&s_tweak_bench[page]);
 TweaksPendingRules(); Menu_AdjustCursor(m,1);
 if (page==7) M_PushMenu(TweaksOldDraw,TweaksOldKey);
 else if (page==6) M_PushMenu(TweaksSimplerDraw,TweaksSimplerKey);
 else if (page==5) M_PushMenu(TweaksTestingDraw,TweaksTestingKey);
 else if (page==4) M_PushMenu(TweaksPathsDraw,TweaksPathsKey);
 else M_PushMenu(page==2?TweaksEnemyDraw:(page==1?TweaksEffectsDraw:TweaksGraphicsDraw),page==2?TweaksEnemyKey:(page==1?TweaksEffectsKey:TweaksGraphicsKey));
}
/* BDGN 3.55i: 0 lighting & shadows, 1 effects, 2 enemies & weapons, 3 TEST low end, 4 draw paths & detail */
void M_Menu_TweaksLighting_f (void) { TweaksOpen(0); }
void M_Menu_TweaksEffects_f (void) { TweaksOpen(1); }
void M_Menu_TweaksEnemies_f (void) { TweaksOpen(2); }
void M_Menu_TweaksPaths_f (void) { TweaksOpen(4); }
void M_Menu_TweaksTesting_f (void) { TweaksOpen(5); }	/* BDGN 3.55ai */
void M_Menu_TweaksSimpler_f (void) { TweaksOpen(6); }	/* BDGN 3.57d */
void M_Menu_TweaksOldSchool_f (void) { TweaksOpen(7); }
void M_Menu_TweaksGraphics_f (void) { TweaksOpen(4); }	/* legacy command entries */
void M_Menu_Performance_f (void) { TweaksOpen(4); }
void M_Menu_Speed_f (void) { TweaksOpen(4); }

/*
=======================================================================

VIDEO MENU

=======================================================================
*/

void M_Menu_Video_f (void)
{
	VID_MenuInit();
	M_PushMenu( VID_MenuDraw, VID_MenuKey );
}

/*
=============================================================================

END GAME MENU

=============================================================================
*/
static int credits_start_time;
static const char **credits;
static char *creditsIndex[256];
static char *creditsBuffer;
static const char *idcredits[] =
{
	"+QUAKE II BY ID SOFTWARE",
	"",
	"+PROGRAMMING",
	"John Carmack",
	"John Cash",
	"Brian Hook",
	"",
	"+ART",
	"Adrian Carmack",
	"Kevin Cloud",
	"Paul Steed",
	"",
	"+LEVEL DESIGN",
	"Tim Willits",
	"American McGee",
	"Christian Antkow",
	"Paul Jaquays",
	"Brandon James",
	"",
	"+BIZ",
	"Todd Hollenshead",
	"Barrett (Bear) Alexander",
	"Donna Jackson",
	"",
	"",
	"+SPECIAL THANKS",
	"Ben Donges for beta testing",
	"",
	"",
	"",
	"",
	"",
	"",
	"+ADDITIONAL SUPPORT",
	"",
	"+LINUX PORT AND CTF",
	"Dave \"Zoid\" Kirsch",
	"",
	"+CINEMATIC SEQUENCES",
	"Ending Cinematic by Blur Studio - ",
	"Venice, CA",
	"",
	"Environment models for Introduction",
	"Cinematic by Karl Dolgener",
	"",
	"Assistance with environment design",
	"by Cliff Iwai",
	"",
	"+SOUND EFFECTS AND MUSIC",
	"Sound Design by Soundelux Media Labs.",
	"Music Composed and Produced by",
	"Soundelux Media Labs.  Special thanks",
	"to Bill Brown, Tom Ozanich, Brian",
	"Celano, Jeff Eisner, and The Soundelux",
	"Players.",
	"",
	"\"Level Music\" by Sonic Mayhem",
	"www.sonicmayhem.com",
	"",
	"\"Quake II Theme Song\"",
	"(C) 1997 Rob Zombie. All Rights",
	"Reserved.",
	"",
	"Track 10 (\"Climb\") by Jer Sypult",
	"",
	"Voice of computers by",
	"Carly Staehlin-Taylor",
	"",
	"+THANKS TO ACTIVISION",
	"+IN PARTICULAR:",
	"",
	"John Tam",
	"Steve Rosenthal",
	"Marty Stratton",
	"Henk Hartong",
	"",
	"Quake II(tm) (C)1997 Id Software, Inc.",
	"All Rights Reserved.  Distributed by",
	"Activision, Inc. under license.",
	"Quake II(tm), the Id Software name,",
	"the \"Q II\"(tm) logo and id(tm)",
	"logo are trademarks of Id Software,",
	"Inc. Activision(R) is a registered",
	"trademark of Activision, Inc. All",
	"other trademarks and trade names are",
	"properties of their respective owners.",
	0
};

static const char *xatcredits[] =
{
	"+QUAKE II MISSION PACK: THE RECKONING",
	"+BY",
	"+XATRIX ENTERTAINMENT, INC.",
	"",
	"+DESIGN AND DIRECTION",
	"Drew Markham",
	"",
	"+PRODUCED BY",
	"Greg Goodrich",
	"",
	"+PROGRAMMING",
	"Rafael Paiz",
	"",
	"+LEVEL DESIGN / ADDITIONAL GAME DESIGN",
	"Alex Mayberry",
	"",
	"+LEVEL DESIGN",
	"Mal Blackwell",
	"Dan Koppel",
	"",
	"+ART DIRECTION",
	"Michael \"Maxx\" Kaufman",
	"",
	"+COMPUTER GRAPHICS SUPERVISOR AND",
	"+CHARACTER ANIMATION DIRECTION",
	"Barry Dempsey",
	"",
	"+SENIOR ANIMATOR AND MODELER",
	"Jason Hoover",
	"",
	"+CHARACTER ANIMATION AND",
	"+MOTION CAPTURE SPECIALIST",
	"Amit Doron",
	"",
	"+ART",
	"Claire Praderie-Markham",
	"Viktor Antonov",
	"Corky Lehmkuhl",
	"",
	"+INTRODUCTION ANIMATION",
	"Dominique Drozdz",
	"",
	"+ADDITIONAL LEVEL DESIGN",
	"Aaron Barber",
	"Rhett Baldwin",
	"",
	"+3D CHARACTER ANIMATION TOOLS",
	"Gerry Tyra, SA Technology",
	"",
	"+ADDITIONAL EDITOR TOOL PROGRAMMING",
	"Robert Duffy",
	"",
	"+ADDITIONAL PROGRAMMING",
	"Ryan Feltrin",
	"",
	"+PRODUCTION COORDINATOR",
	"Victoria Sylvester",
	"",
	"+SOUND DESIGN",
	"Gary Bradfield",
	"",
	"+MUSIC BY",
	"Sonic Mayhem",
	"",
	"",
	"",
	"+SPECIAL THANKS",
	"+TO",
	"+OUR FRIENDS AT ID SOFTWARE",
	"",
	"John Carmack",
	"John Cash",
	"Brian Hook",
	"Adrian Carmack",
	"Kevin Cloud",
	"Paul Steed",
	"Tim Willits",
	"Christian Antkow",
	"Paul Jaquays",
	"Brandon James",
	"Todd Hollenshead",
	"Barrett (Bear) Alexander",
	"Dave \"Zoid\" Kirsch",
	"Donna Jackson",
	"",
	"",
	"",
	"+THANKS TO ACTIVISION",
	"+IN PARTICULAR:",
	"",
	"Marty Stratton",
	"Henk \"The Original Ripper\" Hartong",
	"Kevin Kraff",
	"Jamey Gottlieb",
	"Chris Hepburn",
	"",
	"+AND THE GAME TESTERS",
	"",
	"Tim Vanlaw",
	"Doug Jacobs",
	"Steven Rosenthal",
	"David Baker",
	"Chris Campbell",
	"Aaron Casillas",
	"Steve Elwell",
	"Derek Johnstone",
	"Igor Krinitskiy",
	"Samantha Lee",
	"Michael Spann",
	"Chris Toft",
	"Juan Valdes",
	"",
	"+THANKS TO INTERGRAPH COMPUTER SYTEMS",
	"+IN PARTICULAR:",
	"",
	"Michael T. Nicolaou",
	"",
	"",
	"Quake II Mission Pack: The Reckoning",
	"(tm) (C)1998 Id Software, Inc. All",
	"Rights Reserved. Developed by Xatrix",
	"Entertainment, Inc. for Id Software,",
	"Inc. Distributed by Activision Inc.",
	"under license. Quake(R) is a",
	"registered trademark of Id Software,",
	"Inc. Quake II Mission Pack: The",
	"Reckoning(tm), Quake II(tm), the Id",
	"Software name, the \"Q II\"(tm) logo",
	"and id(tm) logo are trademarks of Id",
	"Software, Inc. Activision(R) is a",
	"registered trademark of Activision,",
	"Inc. Xatrix(R) is a registered",
	"trademark of Xatrix Entertainment,",
	"Inc. All other trademarks and trade",
	"names are properties of their",
	"respective owners.",
	0
};

static const char *roguecredits[] =
{
	"+QUAKE II MISSION PACK 2: GROUND ZERO",
	"+BY",
	"+ROGUE ENTERTAINMENT, INC.",
	"",
	"+PRODUCED BY",
	"Jim Molinets",
	"",
	"+PROGRAMMING",
	"Peter Mack",
	"Patrick Magruder",
	"",
	"+LEVEL DESIGN",
	"Jim Molinets",
	"Cameron Lamprecht",
	"Berenger Fish",
	"Robert Selitto",
	"Steve Tietze",
	"Steve Thoms",
	"",
	"+ART DIRECTION",
	"Rich Fleider",
	"",
	"+ART",
	"Rich Fleider",
	"Steve Maines",
	"Won Choi",
	"",
	"+ANIMATION SEQUENCES",
	"Creat Studios",
	"Steve Maines",
	"",
	"+ADDITIONAL LEVEL DESIGN",
	"Rich Fleider",
	"Steve Maines",
	"Peter Mack",
	"",
	"+SOUND",
	"James Grunke",
	"",
	"+GROUND ZERO THEME",
	"+AND",
	"+MUSIC BY",
	"Sonic Mayhem",
	"",
	"+VWEP MODELS",
	"Brent \"Hentai\" Dill",
	"",
	"",
	"",
	"+SPECIAL THANKS",
	"+TO",
	"+OUR FRIENDS AT ID SOFTWARE",
	"",
	"John Carmack",
	"John Cash",
	"Brian Hook",
	"Adrian Carmack",
	"Kevin Cloud",
	"Paul Steed",
	"Tim Willits",
	"Christian Antkow",
	"Paul Jaquays",
	"Brandon James",
	"Todd Hollenshead",
	"Barrett (Bear) Alexander",
	"Katherine Anna Kang",
	"Donna Jackson",
	"Dave \"Zoid\" Kirsch",
	"",
	"",
	"",
	"+THANKS TO ACTIVISION",
	"+IN PARTICULAR:",
	"",
	"Marty Stratton",
	"Henk Hartong",
	"Mitch Lasky",
	"Steve Rosenthal",
	"Steve Elwell",
	"",
	"+AND THE GAME TESTERS",
	"",
	"The Ranger Clan",
	"Dave \"Zoid\" Kirsch",
	"Nihilistic Software",
	"Robert Duffy",
	"",
	"And Countless Others",
	"",
	"",
	"",
	"Quake II Mission Pack 2: Ground Zero",
	"(tm) (C)1998 Id Software, Inc. All",
	"Rights Reserved. Developed by Rogue",
	"Entertainment, Inc. for Id Software,",
	"Inc. Distributed by Activision Inc.",
	"under license. Quake(R) is a",
	"registered trademark of Id Software,",
	"Inc. Quake II Mission Pack 2: Ground",
	"Zero(tm), Quake II(tm), the Id",
	"Software name, the \"Q II\"(tm) logo",
	"and id(tm) logo are trademarks of Id",
	"Software, Inc. Activision(R) is a",
	"registered trademark of Activision,",
	"Inc. Rogue(R) is a registered",
	"trademark of Rogue Entertainment,",
	"Inc. All other trademarks and trade",
	"names are properties of their",
	"respective owners.",
	0
};


void M_Credits_MenuDraw( void )
{
	int i, y;

	/*
	** draw the credits
	*/
	for ( i = 0, y = viddef.height - ( ( cls.realtime - credits_start_time ) / 40.0F ); credits[i] && y < viddef.height; y += 10, i++ )
	{
		int j, stringoffset = 0;
		int bold = false;

		if ( y <= -8 )
			continue;

		if ( credits[i][0] == '+' )
		{
			bold = true;
			stringoffset = 1;
		}
		else
		{
			bold = false;
			stringoffset = 0;
		}

		for ( j = 0; credits[i][j+stringoffset]; j++ )
		{
			int x;

			x = ( viddef.width - strlen( credits[i] ) * 8 - stringoffset * 8 ) / 2 + ( j + stringoffset ) * 8;

			if ( bold )
				re.DrawChar( x, y, credits[i][j+stringoffset] + 128 );
			else
				re.DrawChar( x, y, credits[i][j+stringoffset] );
		}
	}

	if ( y < 0 )
		credits_start_time = cls.realtime;
}

const char *M_Credits_Key( int key )
{
	switch (key)
	{
	case K_ESCAPE:
		if (creditsBuffer)
			FS_FreeFile (creditsBuffer);
		M_PopMenu ();
		break;
	}

	return menu_out_sound;

}

extern int Developer_searchpath (int who);

void M_Menu_Credits_f( void )
{
	int		n;
	int		count;
	char	*p;
	int		isdeveloper = 0;

	creditsBuffer = NULL;
	count = FS_LoadFile ("credits", &creditsBuffer);
	if (count != -1)
	{
		p = creditsBuffer;
		for (n = 0; n < 255; n++)
		{
			creditsIndex[n] = p;
			while (*p != '\r' && *p != '\n')
			{
				p++;
				if (--count == 0)
					break;
			}
			if (*p == '\r')
			{
				*p++ = 0;
				if (--count == 0)
					break;
			}
			*p++ = 0;
			if (--count == 0)
				break;
		}
		creditsIndex[++n] = 0;
		credits = creditsIndex;
	}
	else
	{
		isdeveloper = Developer_searchpath (1);
		
		if (isdeveloper == 1)			// xatrix
			credits = xatcredits;
		else if (isdeveloper == 2)		// ROGUE
			credits = roguecredits;
		else
		{
			credits = idcredits;	
		}

	}

	/* 3.60i hotfix: project credits precede, and never replace, id's/mod credits. */
	{
		static const char *combined[384];
		static const char *project[] = {
			"+#Quake2ToTheMAX", "Brain Drain / JizzMasta", "Avetar",
			"", "+AI DEVELOPMENT ASSISTANCE", "Claude and ChatGPT",
			"Ideas brought to working code", "", "braindrainlan.com",
			"YouTube / Facebook: Brain Drain LAN", "", NULL
		};
		int i=0,j=0;
		while (project[j]) combined[i++]=project[j++];
		j=0;
		while (credits[j] && i<383) combined[i++]=credits[j++];
		combined[i]=NULL;
		credits=combined;
	}
	credits_start_time = cls.realtime;
	M_PushMenu( M_Credits_MenuDraw, M_Credits_Key);
}

/*
=============================================================================

GAME MENU

=============================================================================
*/

static int		m_game_cursor;

static menuframework_s	s_game_menu;
static menuaction_s		s_easy_game_action;
static menuaction_s		s_medium_game_action;
static menuaction_s		s_hard_game_action;
static menuaction_s		s_load_game_action;
static menuaction_s		s_save_game_action;
static menuaction_s		s_credits_action;
static menuseparator_s	s_blankline;

static void StartGame( void )
{
	// disable updates and start the cinematic going
	cl.servercount = -1;
	M_ForceMenuOff ();
	Cvar_SetValue( "deathmatch", 0 );
	Cvar_SetValue( "coop", 0 );

	Cvar_SetValue( "gamerules", 0 );		//PGM

	Cbuf_AddText ("loading ; killserver ; wait ; newgame\n");
	cls.key_dest = key_game;
}

static void EasyGameFunc( void *data )
{
	Cvar_ForceSet( "skill", "0" );
	StartGame();
}

static void MediumGameFunc( void *data )
{
	Cvar_ForceSet( "skill", "1" );
	StartGame();
}

static void HardGameFunc( void *data )
{
	Cvar_ForceSet( "skill", "2" );
	StartGame();
}

static void LoadGameFunc( void *unused )
{
	M_Menu_LoadGame_f ();
}

static void SaveGameFunc( void *unused )
{
	M_Menu_SaveGame_f();
}

static void CreditsFunc( void *unused )
{
	M_Menu_Credits_f();
}

void Game_MenuInit( void )
{
	static const char *difficulty_names[] =
	{
		"easy",
		"medium",
		"hard",
		0
	};

	s_game_menu.x = viddef.width * 0.50;
	s_game_menu.nitems = 0;

	s_easy_game_action.generic.type	= MTYPE_ACTION;
	s_easy_game_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_easy_game_action.generic.x		= 0;
	s_easy_game_action.generic.y		= 0;
	s_easy_game_action.generic.name	= "easy";
	s_easy_game_action.generic.callback = EasyGameFunc;

	s_medium_game_action.generic.type	= MTYPE_ACTION;
	s_medium_game_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_medium_game_action.generic.x		= 0;
	s_medium_game_action.generic.y		= 10;
	s_medium_game_action.generic.name	= "medium";
	s_medium_game_action.generic.callback = MediumGameFunc;

	s_hard_game_action.generic.type	= MTYPE_ACTION;
	s_hard_game_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_hard_game_action.generic.x		= 0;
	s_hard_game_action.generic.y		= 20;
	s_hard_game_action.generic.name	= "hard";
	s_hard_game_action.generic.callback = HardGameFunc;

	s_blankline.generic.type = MTYPE_SEPARATOR;

	s_load_game_action.generic.type	= MTYPE_ACTION;
	s_load_game_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_load_game_action.generic.x		= 0;
	s_load_game_action.generic.y		= 40;
	s_load_game_action.generic.name	= "load game";
	s_load_game_action.generic.callback = LoadGameFunc;

	s_save_game_action.generic.type	= MTYPE_ACTION;
	s_save_game_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_save_game_action.generic.x		= 0;
	s_save_game_action.generic.y		= 50;
	s_save_game_action.generic.name	= "save game";
	s_save_game_action.generic.callback = SaveGameFunc;

	s_credits_action.generic.type	= MTYPE_ACTION;
	s_credits_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_credits_action.generic.x		= 0;
	s_credits_action.generic.y		= 60;
	s_credits_action.generic.name	= "credits";
	s_credits_action.generic.callback = CreditsFunc;

	Menu_AddItem( &s_game_menu, ( void * ) &s_easy_game_action );
	Menu_AddItem( &s_game_menu, ( void * ) &s_medium_game_action );
	Menu_AddItem( &s_game_menu, ( void * ) &s_hard_game_action );
	Menu_AddItem( &s_game_menu, ( void * ) &s_blankline );
	Menu_AddItem( &s_game_menu, ( void * ) &s_load_game_action );
	Menu_AddItem( &s_game_menu, ( void * ) &s_save_game_action );
	Menu_AddItem( &s_game_menu, ( void * ) &s_blankline );
	Menu_AddItem( &s_game_menu, ( void * ) &s_credits_action );

	Menu_Center( &s_game_menu );
}

void Game_MenuDraw( void )
{
	M_Banner( "m_banner_game" );
	Menu_AdjustCursor( &s_game_menu, 1 );
	Menu_Draw( &s_game_menu );
}

const char *Game_MenuKey( int key )
{
	return Default_MenuKey( &s_game_menu, key );
}

void M_Menu_Game_f (void)
{
	Game_MenuInit();
	M_PushMenu( Game_MenuDraw, Game_MenuKey );
	m_game_cursor = 1;
}

/*
=============================================================================

LOADGAME MENU

=============================================================================
*/

#define	MAX_SAVEGAMES	15

static menuframework_s	s_savegame_menu;

static menuframework_s	s_loadgame_menu;
static menuaction_s		s_loadgame_actions[MAX_SAVEGAMES];

char		m_savestrings[MAX_SAVEGAMES][32];
qboolean	m_savevalid[MAX_SAVEGAMES];

void Create_Savestrings (void)
{
	int		i;
	FILE	*f;
	char	name[MAX_OSPATH];

	for (i=0 ; i<MAX_SAVEGAMES ; i++)
	{
		Com_sprintf (name, sizeof(name), "%s/save/save%i/server.ssv", FS_Gamedir(), i);
		f = fopen (name, "rb");
		if (!f)
		{
			strcpy (m_savestrings[i], "<EMPTY>");
			m_savevalid[i] = false;
		}
		else
		{
			FS_Read (m_savestrings[i], sizeof(m_savestrings[i]), f);
			fclose (f);
			m_savevalid[i] = true;
		}
	}
}

void LoadGameCallback( void *self )
{
	menuaction_s *a = ( menuaction_s * ) self;

	if ( m_savevalid[ a->generic.localdata[0] ] )
		Cbuf_AddText (va("load save%i\n",  a->generic.localdata[0] ) );
	M_ForceMenuOff ();
}

void LoadGame_MenuInit( void )
{
	int i;

	s_loadgame_menu.x = viddef.width / 2 - 120;
	s_loadgame_menu.y = viddef.height / 2 - 58;
	s_loadgame_menu.nitems = 0;

	Create_Savestrings();

	for ( i = 0; i < MAX_SAVEGAMES; i++ )
	{
		s_loadgame_actions[i].generic.name			= m_savestrings[i];
		s_loadgame_actions[i].generic.flags			= QMF_LEFT_JUSTIFY;
		s_loadgame_actions[i].generic.localdata[0]	= i;
		s_loadgame_actions[i].generic.callback		= LoadGameCallback;

		s_loadgame_actions[i].generic.x = 0;
		s_loadgame_actions[i].generic.y = ( i ) * 10;
		if (i>0)	// separate from autosave
			s_loadgame_actions[i].generic.y += 10;

		s_loadgame_actions[i].generic.type = MTYPE_ACTION;

		Menu_AddItem( &s_loadgame_menu, &s_loadgame_actions[i] );
	}
}

void LoadGame_MenuDraw( void )
{
	M_Banner( "m_banner_load_game" );
//	Menu_AdjustCursor( &s_loadgame_menu, 1 );
	Menu_Draw( &s_loadgame_menu );
}

const char *LoadGame_MenuKey( int key )
{
	if ( key == K_ESCAPE || key == K_ENTER )
	{
		s_savegame_menu.cursor = s_loadgame_menu.cursor - 1;
		if ( s_savegame_menu.cursor < 0 )
			s_savegame_menu.cursor = 0;
	}
	return Default_MenuKey( &s_loadgame_menu, key );
}

void M_Menu_LoadGame_f (void)
{
	LoadGame_MenuInit();
	M_PushMenu( LoadGame_MenuDraw, LoadGame_MenuKey );
}


/*
=============================================================================

SAVEGAME MENU

=============================================================================
*/
static menuframework_s	s_savegame_menu;
static menuaction_s		s_savegame_actions[MAX_SAVEGAMES];

void SaveGameCallback( void *self )
{
	menuaction_s *a = ( menuaction_s * ) self;

	Cbuf_AddText (va("save save%i\n", a->generic.localdata[0] ));
	M_ForceMenuOff ();
}

void SaveGame_MenuDraw( void )
{
	M_Banner( "m_banner_save_game" );
	Menu_AdjustCursor( &s_savegame_menu, 1 );
	Menu_Draw( &s_savegame_menu );
}

void SaveGame_MenuInit( void )
{
	int i;

	s_savegame_menu.x = viddef.width / 2 - 120;
	s_savegame_menu.y = viddef.height / 2 - 58;
	s_savegame_menu.nitems = 0;

	Create_Savestrings();

	// don't include the autosave slot
	for ( i = 0; i < MAX_SAVEGAMES-1; i++ )
	{
		s_savegame_actions[i].generic.name = m_savestrings[i+1];
		s_savegame_actions[i].generic.localdata[0] = i+1;
		s_savegame_actions[i].generic.flags = QMF_LEFT_JUSTIFY;
		s_savegame_actions[i].generic.callback = SaveGameCallback;

		s_savegame_actions[i].generic.x = 0;
		s_savegame_actions[i].generic.y = ( i ) * 10;

		s_savegame_actions[i].generic.type = MTYPE_ACTION;

		Menu_AddItem( &s_savegame_menu, &s_savegame_actions[i] );
	}
}

const char *SaveGame_MenuKey( int key )
{
	if ( key == K_ENTER || key == K_ESCAPE )
	{
		s_loadgame_menu.cursor = s_savegame_menu.cursor - 1;
		if ( s_loadgame_menu.cursor < 0 )
			s_loadgame_menu.cursor = 0;
	}
	return Default_MenuKey( &s_savegame_menu, key );
}

void M_Menu_SaveGame_f (void)
{
	if (!Com_ServerState())
		return;		// not playing a game

	SaveGame_MenuInit();
	M_PushMenu( SaveGame_MenuDraw, SaveGame_MenuKey );
	Create_Savestrings ();
}


/*
=============================================================================

JOIN SERVER MENU

=============================================================================
*/
#define MAX_LOCAL_SERVERS 8

static menuframework_s	s_joinserver_menu;
static menuseparator_s	s_joinserver_server_title;
static menuaction_s		s_joinserver_search_action;
static menuaction_s		s_joinserver_address_book_action;
static menuaction_s		s_joinserver_server_actions[MAX_LOCAL_SERVERS];

int		m_num_servers;
#define	NO_SERVER_STRING	"<no server>"

// user readable information
static char local_server_names[MAX_LOCAL_SERVERS][80];

// network address
static netadr_t local_server_netadr[MAX_LOCAL_SERVERS];

void M_AddToServerList (netadr_t adr, char *info)
{
	int		i;

	if (m_num_servers == MAX_LOCAL_SERVERS)
		return;
	while ( *info == ' ' )
		info++;

	// ignore if duplicated
	for (i=0 ; i<m_num_servers ; i++)
		if (!strcmp(info, local_server_names[i]))
			return;

	local_server_netadr[m_num_servers] = adr;
	strncpy (local_server_names[m_num_servers], info, sizeof(local_server_names[0])-1);
	m_num_servers++;
}


void JoinServerFunc( void *self )
{
	char	buffer[128];
	int		index;

	index = ( menuaction_s * ) self - s_joinserver_server_actions;

	if ( Q_stricmp( local_server_names[index], NO_SERVER_STRING ) == 0 )
		return;

	if (index >= m_num_servers)
		return;

	Com_sprintf (buffer, sizeof(buffer), "connect %s\n", NET_AdrToString (local_server_netadr[index]));
	Cbuf_AddText (buffer);
	M_ForceMenuOff ();
}

void AddressBookFunc( void *self )
{
	M_Menu_AddressBook_f();
}

void NullCursorDraw( void *self )
{
}

void SearchLocalGames( void )
{
	int		i;

	m_num_servers = 0;
	for (i=0 ; i<MAX_LOCAL_SERVERS ; i++)
		strcpy (local_server_names[i], NO_SERVER_STRING);

	M_DrawTextBox( 8, 120 - 48, 36, 3 );
	M_Print( 16 + 16, 120 - 48 + 8,  "Searching for local servers, this" );
	M_Print( 16 + 16, 120 - 48 + 16, "could take up to a minute, so" );
	M_Print( 16 + 16, 120 - 48 + 24, "please be patient." );

	// the text box won't show up unless we do a buffer swap
	re.EndFrame();

	// send out info packets
	CL_PingServers_f();
}

void SearchLocalGamesFunc( void *self )
{
	SearchLocalGames();
}

void JoinServer_MenuInit( void )
{
	int i;

	s_joinserver_menu.x = viddef.width * 0.50 - 120;
	s_joinserver_menu.nitems = 0;

	s_joinserver_address_book_action.generic.type	= MTYPE_ACTION;
	s_joinserver_address_book_action.generic.name	= "address book";
	s_joinserver_address_book_action.generic.flags	= QMF_LEFT_JUSTIFY;
	s_joinserver_address_book_action.generic.x		= 0;
	s_joinserver_address_book_action.generic.y		= 0;
	s_joinserver_address_book_action.generic.callback = AddressBookFunc;

	s_joinserver_search_action.generic.type = MTYPE_ACTION;
	s_joinserver_search_action.generic.name	= "refresh server list";
	s_joinserver_search_action.generic.flags	= QMF_LEFT_JUSTIFY;
	s_joinserver_search_action.generic.x	= 0;
	s_joinserver_search_action.generic.y	= 10;
	s_joinserver_search_action.generic.callback = SearchLocalGamesFunc;
	s_joinserver_search_action.generic.statusbar = "search for servers";

	s_joinserver_server_title.generic.type = MTYPE_SEPARATOR;
	s_joinserver_server_title.generic.name = "connect to...";
	s_joinserver_server_title.generic.x    = 80;
	s_joinserver_server_title.generic.y	   = 30;

	for ( i = 0; i < MAX_LOCAL_SERVERS; i++ )
	{
		s_joinserver_server_actions[i].generic.type	= MTYPE_ACTION;
		strcpy (local_server_names[i], NO_SERVER_STRING);
		s_joinserver_server_actions[i].generic.name	= local_server_names[i];
		s_joinserver_server_actions[i].generic.flags	= QMF_LEFT_JUSTIFY;
		s_joinserver_server_actions[i].generic.x		= 0;
		s_joinserver_server_actions[i].generic.y		= 40 + i*10;
		s_joinserver_server_actions[i].generic.callback = JoinServerFunc;
		s_joinserver_server_actions[i].generic.statusbar = "press ENTER to connect";
	}

	Menu_AddItem( &s_joinserver_menu, &s_joinserver_address_book_action );
	Menu_AddItem( &s_joinserver_menu, &s_joinserver_server_title );
	Menu_AddItem( &s_joinserver_menu, &s_joinserver_search_action );

	for ( i = 0; i < 8; i++ )
		Menu_AddItem( &s_joinserver_menu, &s_joinserver_server_actions[i] );

	Menu_Center( &s_joinserver_menu );

	SearchLocalGames();
}

void JoinServer_MenuDraw(void)
{
	M_Banner( "m_banner_join_server" );
	Menu_Draw( &s_joinserver_menu );
}


const char *JoinServer_MenuKey( int key )
{
	return Default_MenuKey( &s_joinserver_menu, key );
}

void M_Menu_JoinServer_f (void)
{
	JoinServer_MenuInit();
	M_PushMenu( JoinServer_MenuDraw, JoinServer_MenuKey );
}


/*
=============================================================================

START SERVER MENU

=============================================================================
*/
static menuframework_s s_startserver_menu;
static char **mapnames;
static int	  nummaps;

static menuaction_s	s_startserver_start_action;
static menuaction_s	s_startserver_dmoptions_action;
static menufield_s	s_timelimit_field;
static menufield_s	s_fraglimit_field;
static menufield_s	s_maxclients_field;
static menufield_s	s_hostname_field;
static menulist_s	s_startmap_list;
static menulist_s	s_rules_box;

void DMOptionsFunc( void *self )
{
	if (s_rules_box.curvalue == 1)
		return;
	M_Menu_DMOptions_f();
}

void RulesChangeFunc ( void *self )
{
	// DM
	if (s_rules_box.curvalue == 0)
	{
		s_maxclients_field.generic.statusbar = NULL;
		s_startserver_dmoptions_action.generic.statusbar = NULL;
	}
	else if(s_rules_box.curvalue == 1)		// coop				// PGM
	{
		s_maxclients_field.generic.statusbar = "4 maximum for cooperative";
		if (atoi(s_maxclients_field.buffer) > 4)
			strcpy( s_maxclients_field.buffer, "4" );
		s_startserver_dmoptions_action.generic.statusbar = "N/A for cooperative";
	}
//=====
//PGM
	// ROGUE GAMES
	else if(Developer_searchpath(2) == 2)
	{
		if (s_rules_box.curvalue == 2)			// tag	
		{
			s_maxclients_field.generic.statusbar = NULL;
			s_startserver_dmoptions_action.generic.statusbar = NULL;
		}
/*
		else if(s_rules_box.curvalue == 3)		// deathball
		{
			s_maxclients_field.generic.statusbar = NULL;
			s_startserver_dmoptions_action.generic.statusbar = NULL;
		}
*/
	}
//PGM
//=====
}

void StartServerActionFunc( void *self )
{
	char	startmap[1024];
	int		timelimit;
	int		fraglimit;
	int		maxclients;
	char	*spot;

	strcpy( startmap, strchr( mapnames[s_startmap_list.curvalue], '\n' ) + 1 );

	maxclients  = atoi( s_maxclients_field.buffer );
	timelimit	= atoi( s_timelimit_field.buffer );
	fraglimit	= atoi( s_fraglimit_field.buffer );

	Cvar_SetValue( "maxclients", ClampCvar( 0, maxclients, maxclients ) );
	Cvar_SetValue ("timelimit", ClampCvar( 0, timelimit, timelimit ) );
	Cvar_SetValue ("fraglimit", ClampCvar( 0, fraglimit, fraglimit ) );
	Cvar_Set("hostname", s_hostname_field.buffer );
//	Cvar_SetValue ("deathmatch", !s_rules_box.curvalue );
//	Cvar_SetValue ("coop", s_rules_box.curvalue );

//PGM
	if((s_rules_box.curvalue < 2) || (Developer_searchpath(2) != 2))
	{
		Cvar_SetValue ("deathmatch", !s_rules_box.curvalue );
		Cvar_SetValue ("coop", s_rules_box.curvalue );
		Cvar_SetValue ("gamerules", 0 );
	}
	else
	{
		Cvar_SetValue ("deathmatch", 1 );	// deathmatch is always true for rogue games, right?
		Cvar_SetValue ("coop", 0 );			// FIXME - this might need to depend on which game we're running
		Cvar_SetValue ("gamerules", s_rules_box.curvalue );
	}
//PGM

	spot = NULL;
	if (s_rules_box.curvalue == 1)		// PGM
	{
 		if(Q_stricmp(startmap, "bunk1") == 0)
  			spot = "start";
 		else if(Q_stricmp(startmap, "mintro") == 0)
  			spot = "start";
 		else if(Q_stricmp(startmap, "fact1") == 0)
  			spot = "start";
 		else if(Q_stricmp(startmap, "power1") == 0)
  			spot = "pstart";
 		else if(Q_stricmp(startmap, "biggun") == 0)
  			spot = "bstart";
 		else if(Q_stricmp(startmap, "hangar1") == 0)
  			spot = "unitstart";
 		else if(Q_stricmp(startmap, "city1") == 0)
  			spot = "unitstart";
 		else if(Q_stricmp(startmap, "boss1") == 0)
			spot = "bosstart";
	}

	if (spot)
	{
		if (Com_ServerState())
			Cbuf_AddText ("disconnect\n");
		Cbuf_AddText (va("gamemap \"*%s$%s\"\n", startmap, spot));
	}
	else
	{
		Cbuf_AddText (va("map %s\n", startmap));
	}

	M_ForceMenuOff ();
}

void StartServer_MenuInit( void )
{
	static const char *dm_coop_names[] =
	{
		"deathmatch",
		"cooperative",
		0
	};
//=======
//PGM
	static const char *dm_coop_names_rogue[] =
	{
		"deathmatch",
		"cooperative",
		"tag",
//		"deathball",
		0
	};
//PGM
//=======
	char *buffer;
	char  mapsname[1024];
	char *s;
	int length;
	int i;
	FILE *fp;

	/*
	** load the list of map names
	*/
	Com_sprintf( mapsname, sizeof( mapsname ), "%s/maps.lst", FS_Gamedir() );
	if ( ( fp = fopen( mapsname, "rb" ) ) == 0 )
	{
		if ( ( length = FS_LoadFile( "maps.lst", ( void ** ) &buffer ) ) == -1 )
			Com_Error( ERR_DROP, "couldn't find maps.lst\n" );
	}
	else
	{
#ifdef _WIN32
		length = filelength( fileno( fp  ) );
#else
		fseek(fp, 0, SEEK_END);
		length = ftell(fp);
		fseek(fp, 0, SEEK_SET);
#endif
		buffer = malloc( length );
		fread( buffer, length, 1, fp );
	}

	s = buffer;

	i = 0;
	while ( i < length )
	{
		if ( s[i] == '\r' )
			nummaps++;
		i++;
	}

	if ( nummaps == 0 )
		Com_Error( ERR_DROP, "no maps in maps.lst\n" );

	mapnames = malloc( sizeof( char * ) * ( nummaps + 1 ) );
	memset( mapnames, 0, sizeof( char * ) * ( nummaps + 1 ) );

	s = buffer;

	for ( i = 0; i < nummaps; i++ )
	{
    char  shortname[MAX_TOKEN_CHARS];
    char  longname[MAX_TOKEN_CHARS];
		char  scratch[200];
		int		j, l;

		strcpy( shortname, COM_Parse( &s ) );
		l = strlen(shortname);
		for (j=0 ; j<l ; j++)
			shortname[j] = toupper(shortname[j]);
		strcpy( longname, COM_Parse( &s ) );
		Com_sprintf( scratch, sizeof( scratch ), "%s\n%s", longname, shortname );

		mapnames[i] = malloc( strlen( scratch ) + 1 );
		strcpy( mapnames[i], scratch );
	}
	mapnames[nummaps] = 0;

	if ( fp != 0 )
	{
		fp = 0;
		free( buffer );
	}
	else
	{
		FS_FreeFile( buffer );
	}

	/*
	** initialize the menu stuff
	*/
	s_startserver_menu.x = viddef.width * 0.50;
	s_startserver_menu.nitems = 0;

	s_startmap_list.generic.type = MTYPE_SPINCONTROL;
	s_startmap_list.generic.x	= 0;
	s_startmap_list.generic.y	= 0;
	s_startmap_list.generic.name	= "initial map";
	s_startmap_list.itemnames = mapnames;

	s_rules_box.generic.type = MTYPE_SPINCONTROL;
	s_rules_box.generic.x	= 0;
	s_rules_box.generic.y	= 20;
	s_rules_box.generic.name	= "rules";
	
//PGM - rogue games only available with rogue DLL.
	if(Developer_searchpath(2) == 2)
		s_rules_box.itemnames = dm_coop_names_rogue;
	else
		s_rules_box.itemnames = dm_coop_names;
//PGM

	if (Cvar_VariableValue("coop"))
		s_rules_box.curvalue = 1;
	else
		s_rules_box.curvalue = 0;
	s_rules_box.generic.callback = RulesChangeFunc;

	s_timelimit_field.generic.type = MTYPE_FIELD;
	s_timelimit_field.generic.name = "time limit";
	s_timelimit_field.generic.flags = QMF_NUMBERSONLY;
	s_timelimit_field.generic.x	= 0;
	s_timelimit_field.generic.y	= 36;
	s_timelimit_field.generic.statusbar = "0 = no limit";
	s_timelimit_field.length = 3;
	s_timelimit_field.visible_length = 3;
	strcpy( s_timelimit_field.buffer, Cvar_VariableString("timelimit") );

	s_fraglimit_field.generic.type = MTYPE_FIELD;
	s_fraglimit_field.generic.name = "frag limit";
	s_fraglimit_field.generic.flags = QMF_NUMBERSONLY;
	s_fraglimit_field.generic.x	= 0;
	s_fraglimit_field.generic.y	= 54;
	s_fraglimit_field.generic.statusbar = "0 = no limit";
	s_fraglimit_field.length = 3;
	s_fraglimit_field.visible_length = 3;
	strcpy( s_fraglimit_field.buffer, Cvar_VariableString("fraglimit") );

	/*
	** maxclients determines the maximum number of players that can join
	** the game.  If maxclients is only "1" then we should default the menu
	** option to 8 players, otherwise use whatever its current value is. 
	** Clamping will be done when the server is actually started.
	*/
	s_maxclients_field.generic.type = MTYPE_FIELD;
	s_maxclients_field.generic.name = "max players";
	s_maxclients_field.generic.flags = QMF_NUMBERSONLY;
	s_maxclients_field.generic.x	= 0;
	s_maxclients_field.generic.y	= 72;
	s_maxclients_field.generic.statusbar = NULL;
	s_maxclients_field.length = 3;
	s_maxclients_field.visible_length = 3;
	if ( Cvar_VariableValue( "maxclients" ) == 1 )
		strcpy( s_maxclients_field.buffer, "8" );
	else 
		strcpy( s_maxclients_field.buffer, Cvar_VariableString("maxclients") );

	s_hostname_field.generic.type = MTYPE_FIELD;
	s_hostname_field.generic.name = "hostname";
	s_hostname_field.generic.flags = 0;
	s_hostname_field.generic.x	= 0;
	s_hostname_field.generic.y	= 90;
	s_hostname_field.generic.statusbar = NULL;
	s_hostname_field.length = 12;
	s_hostname_field.visible_length = 12;
	strcpy( s_hostname_field.buffer, Cvar_VariableString("hostname") );

	s_startserver_dmoptions_action.generic.type = MTYPE_ACTION;
	s_startserver_dmoptions_action.generic.name	= " deathmatch flags";
	s_startserver_dmoptions_action.generic.flags= QMF_LEFT_JUSTIFY;
	s_startserver_dmoptions_action.generic.x	= 24;
	s_startserver_dmoptions_action.generic.y	= 108;
	s_startserver_dmoptions_action.generic.statusbar = NULL;
	s_startserver_dmoptions_action.generic.callback = DMOptionsFunc;

	s_startserver_start_action.generic.type = MTYPE_ACTION;
	s_startserver_start_action.generic.name	= " begin";
	s_startserver_start_action.generic.flags= QMF_LEFT_JUSTIFY;
	s_startserver_start_action.generic.x	= 24;
	s_startserver_start_action.generic.y	= 128;
	s_startserver_start_action.generic.callback = StartServerActionFunc;

	Menu_AddItem( &s_startserver_menu, &s_startmap_list );
	Menu_AddItem( &s_startserver_menu, &s_rules_box );
	Menu_AddItem( &s_startserver_menu, &s_timelimit_field );
	Menu_AddItem( &s_startserver_menu, &s_fraglimit_field );
	Menu_AddItem( &s_startserver_menu, &s_maxclients_field );
	Menu_AddItem( &s_startserver_menu, &s_hostname_field );
	Menu_AddItem( &s_startserver_menu, &s_startserver_dmoptions_action );
	Menu_AddItem( &s_startserver_menu, &s_startserver_start_action );

	Menu_Center( &s_startserver_menu );

	// call this now to set proper inital state
	RulesChangeFunc ( NULL );
}

void StartServer_MenuDraw(void)
{
	Menu_Draw( &s_startserver_menu );
}

const char *StartServer_MenuKey( int key )
{
	if ( key == K_ESCAPE )
	{
		if ( mapnames )
		{
			int i;

			for ( i = 0; i < nummaps; i++ )
				free( mapnames[i] );
			free( mapnames );
		}
		mapnames = 0;
		nummaps = 0;
	}

	return Default_MenuKey( &s_startserver_menu, key );
}

void M_Menu_StartServer_f (void)
{
	StartServer_MenuInit();
	M_PushMenu( StartServer_MenuDraw, StartServer_MenuKey );
}

/*
=============================================================================

DMOPTIONS BOOK MENU

=============================================================================
*/
static char dmoptions_statusbar[128];

static menuframework_s s_dmoptions_menu;

static menulist_s	s_friendlyfire_box;
static menulist_s	s_falls_box;
static menulist_s	s_weapons_stay_box;
static menulist_s	s_instant_powerups_box;
static menulist_s	s_powerups_box;
static menulist_s	s_health_box;
static menulist_s	s_spawn_farthest_box;
static menulist_s	s_teamplay_box;
static menulist_s	s_samelevel_box;
static menulist_s	s_force_respawn_box;
static menulist_s	s_armor_box;
static menulist_s	s_allow_exit_box;
static menulist_s	s_infinite_ammo_box;
static menulist_s	s_fixed_fov_box;
static menulist_s	s_quad_drop_box;

//ROGUE
static menulist_s	s_no_mines_box;
static menulist_s	s_no_nukes_box;
static menulist_s	s_stack_double_box;
static menulist_s	s_no_spheres_box;
//ROGUE

static void DMFlagCallback( void *self )
{
	menulist_s *f = ( menulist_s * ) self;
	int flags;
	int bit = 0;

	flags = Cvar_VariableValue( "dmflags" );

	if ( f == &s_friendlyfire_box )
	{
		if ( f->curvalue )
			flags &= ~DF_NO_FRIENDLY_FIRE;
		else
			flags |= DF_NO_FRIENDLY_FIRE;
		goto setvalue;
	}
	else if ( f == &s_falls_box )
	{
		if ( f->curvalue )
			flags &= ~DF_NO_FALLING;
		else
			flags |= DF_NO_FALLING;
		goto setvalue;
	}
	else if ( f == &s_weapons_stay_box ) 
	{
		bit = DF_WEAPONS_STAY;
	}
	else if ( f == &s_instant_powerups_box )
	{
		bit = DF_INSTANT_ITEMS;
	}
	else if ( f == &s_allow_exit_box )
	{
		bit = DF_ALLOW_EXIT;
	}
	else if ( f == &s_powerups_box )
	{
		if ( f->curvalue )
			flags &= ~DF_NO_ITEMS;
		else
			flags |= DF_NO_ITEMS;
		goto setvalue;
	}
	else if ( f == &s_health_box )
	{
		if ( f->curvalue )
			flags &= ~DF_NO_HEALTH;
		else
			flags |= DF_NO_HEALTH;
		goto setvalue;
	}
	else if ( f == &s_spawn_farthest_box )
	{
		bit = DF_SPAWN_FARTHEST;
	}
	else if ( f == &s_teamplay_box )
	{
		if ( f->curvalue == 1 )
		{
			flags |=  DF_SKINTEAMS;
			flags &= ~DF_MODELTEAMS;
		}
		else if ( f->curvalue == 2 )
		{
			flags |=  DF_MODELTEAMS;
			flags &= ~DF_SKINTEAMS;
		}
		else
		{
			flags &= ~( DF_MODELTEAMS | DF_SKINTEAMS );
		}

		goto setvalue;
	}
	else if ( f == &s_samelevel_box )
	{
		bit = DF_SAME_LEVEL;
	}
	else if ( f == &s_force_respawn_box )
	{
		bit = DF_FORCE_RESPAWN;
	}
	else if ( f == &s_armor_box )
	{
		if ( f->curvalue )
			flags &= ~DF_NO_ARMOR;
		else
			flags |= DF_NO_ARMOR;
		goto setvalue;
	}
	else if ( f == &s_infinite_ammo_box )
	{
		bit = DF_INFINITE_AMMO;
	}
	else if ( f == &s_fixed_fov_box )
	{
		bit = DF_FIXED_FOV;
	}
	else if ( f == &s_quad_drop_box )
	{
		bit = DF_QUAD_DROP;
	}

//=======
//ROGUE
	else if (Developer_searchpath(2) == 2)
	{
		if ( f == &s_no_mines_box)
		{
			bit = DF_NO_MINES;
		}
		else if ( f == &s_no_nukes_box)
		{
			bit = DF_NO_NUKES;
		}
		else if ( f == &s_stack_double_box)
		{
			bit = DF_NO_STACK_DOUBLE;
		}
		else if ( f == &s_no_spheres_box)
		{
			bit = DF_NO_SPHERES;
		}
	}
//ROGUE
//=======

	if ( f )
	{
		if ( f->curvalue == 0 )
			flags &= ~bit;
		else
			flags |= bit;
	}

setvalue:
	Cvar_SetValue ("dmflags", flags);

	Com_sprintf( dmoptions_statusbar, sizeof( dmoptions_statusbar ), "dmflags = %d", flags );

}

void DMOptions_MenuInit( void )
{
	static const char *yes_no_names[] =
	{
		"no", "yes", 0
	};
	static const char *teamplay_names[] = 
	{
		"disabled", "by skin", "by model", 0
	};
	int dmflags = Cvar_VariableValue( "dmflags" );
	int y = 0;

	s_dmoptions_menu.x = viddef.width * 0.50;
	s_dmoptions_menu.nitems = 0;

	s_falls_box.generic.type = MTYPE_SPINCONTROL;
	s_falls_box.generic.x	= 0;
	s_falls_box.generic.y	= y;
	s_falls_box.generic.name	= "falling damage";
	s_falls_box.generic.callback = DMFlagCallback;
	s_falls_box.itemnames = yes_no_names;
	s_falls_box.curvalue = ( dmflags & DF_NO_FALLING ) == 0;

	s_weapons_stay_box.generic.type = MTYPE_SPINCONTROL;
	s_weapons_stay_box.generic.x	= 0;
	s_weapons_stay_box.generic.y	= y += 10;
	s_weapons_stay_box.generic.name	= "weapons stay";
	s_weapons_stay_box.generic.callback = DMFlagCallback;
	s_weapons_stay_box.itemnames = yes_no_names;
	s_weapons_stay_box.curvalue = ( dmflags & DF_WEAPONS_STAY ) != 0;

	s_instant_powerups_box.generic.type = MTYPE_SPINCONTROL;
	s_instant_powerups_box.generic.x	= 0;
	s_instant_powerups_box.generic.y	= y += 10;
	s_instant_powerups_box.generic.name	= "instant powerups";
	s_instant_powerups_box.generic.callback = DMFlagCallback;
	s_instant_powerups_box.itemnames = yes_no_names;
	s_instant_powerups_box.curvalue = ( dmflags & DF_INSTANT_ITEMS ) != 0;

	s_powerups_box.generic.type = MTYPE_SPINCONTROL;
	s_powerups_box.generic.x	= 0;
	s_powerups_box.generic.y	= y += 10;
	s_powerups_box.generic.name	= "allow powerups";
	s_powerups_box.generic.callback = DMFlagCallback;
	s_powerups_box.itemnames = yes_no_names;
	s_powerups_box.curvalue = ( dmflags & DF_NO_ITEMS ) == 0;

	s_health_box.generic.type = MTYPE_SPINCONTROL;
	s_health_box.generic.x	= 0;
	s_health_box.generic.y	= y += 10;
	s_health_box.generic.callback = DMFlagCallback;
	s_health_box.generic.name	= "allow health";
	s_health_box.itemnames = yes_no_names;
	s_health_box.curvalue = ( dmflags & DF_NO_HEALTH ) == 0;

	s_armor_box.generic.type = MTYPE_SPINCONTROL;
	s_armor_box.generic.x	= 0;
	s_armor_box.generic.y	= y += 10;
	s_armor_box.generic.name	= "allow armor";
	s_armor_box.generic.callback = DMFlagCallback;
	s_armor_box.itemnames = yes_no_names;
	s_armor_box.curvalue = ( dmflags & DF_NO_ARMOR ) == 0;

	s_spawn_farthest_box.generic.type = MTYPE_SPINCONTROL;
	s_spawn_farthest_box.generic.x	= 0;
	s_spawn_farthest_box.generic.y	= y += 10;
	s_spawn_farthest_box.generic.name	= "spawn farthest";
	s_spawn_farthest_box.generic.callback = DMFlagCallback;
	s_spawn_farthest_box.itemnames = yes_no_names;
	s_spawn_farthest_box.curvalue = ( dmflags & DF_SPAWN_FARTHEST ) != 0;

	s_samelevel_box.generic.type = MTYPE_SPINCONTROL;
	s_samelevel_box.generic.x	= 0;
	s_samelevel_box.generic.y	= y += 10;
	s_samelevel_box.generic.name	= "same map";
	s_samelevel_box.generic.callback = DMFlagCallback;
	s_samelevel_box.itemnames = yes_no_names;
	s_samelevel_box.curvalue = ( dmflags & DF_SAME_LEVEL ) != 0;

	s_force_respawn_box.generic.type = MTYPE_SPINCONTROL;
	s_force_respawn_box.generic.x	= 0;
	s_force_respawn_box.generic.y	= y += 10;
	s_force_respawn_box.generic.name	= "force respawn";
	s_force_respawn_box.generic.callback = DMFlagCallback;
	s_force_respawn_box.itemnames = yes_no_names;
	s_force_respawn_box.curvalue = ( dmflags & DF_FORCE_RESPAWN ) != 0;

	s_teamplay_box.generic.type = MTYPE_SPINCONTROL;
	s_teamplay_box.generic.x	= 0;
	s_teamplay_box.generic.y	= y += 10;
	s_teamplay_box.generic.name	= "teamplay";
	s_teamplay_box.generic.callback = DMFlagCallback;
	s_teamplay_box.itemnames = teamplay_names;

	s_allow_exit_box.generic.type = MTYPE_SPINCONTROL;
	s_allow_exit_box.generic.x	= 0;
	s_allow_exit_box.generic.y	= y += 10;
	s_allow_exit_box.generic.name	= "allow exit";
	s_allow_exit_box.generic.callback = DMFlagCallback;
	s_allow_exit_box.itemnames = yes_no_names;
	s_allow_exit_box.curvalue = ( dmflags & DF_ALLOW_EXIT ) != 0;

	s_infinite_ammo_box.generic.type = MTYPE_SPINCONTROL;
	s_infinite_ammo_box.generic.x	= 0;
	s_infinite_ammo_box.generic.y	= y += 10;
	s_infinite_ammo_box.generic.name	= "infinite ammo";
	s_infinite_ammo_box.generic.callback = DMFlagCallback;
	s_infinite_ammo_box.itemnames = yes_no_names;
	s_infinite_ammo_box.curvalue = ( dmflags & DF_INFINITE_AMMO ) != 0;

	s_fixed_fov_box.generic.type = MTYPE_SPINCONTROL;
	s_fixed_fov_box.generic.x	= 0;
	s_fixed_fov_box.generic.y	= y += 10;
	s_fixed_fov_box.generic.name	= "fixed FOV";
	s_fixed_fov_box.generic.callback = DMFlagCallback;
	s_fixed_fov_box.itemnames = yes_no_names;
	s_fixed_fov_box.curvalue = ( dmflags & DF_FIXED_FOV ) != 0;

	s_quad_drop_box.generic.type = MTYPE_SPINCONTROL;
	s_quad_drop_box.generic.x	= 0;
	s_quad_drop_box.generic.y	= y += 10;
	s_quad_drop_box.generic.name	= "quad drop";
	s_quad_drop_box.generic.callback = DMFlagCallback;
	s_quad_drop_box.itemnames = yes_no_names;
	s_quad_drop_box.curvalue = ( dmflags & DF_QUAD_DROP ) != 0;

	s_friendlyfire_box.generic.type = MTYPE_SPINCONTROL;
	s_friendlyfire_box.generic.x	= 0;
	s_friendlyfire_box.generic.y	= y += 10;
	s_friendlyfire_box.generic.name	= "friendly fire";
	s_friendlyfire_box.generic.callback = DMFlagCallback;
	s_friendlyfire_box.itemnames = yes_no_names;
	s_friendlyfire_box.curvalue = ( dmflags & DF_NO_FRIENDLY_FIRE ) == 0;

//============
//ROGUE
	if(Developer_searchpath(2) == 2)
	{
		s_no_mines_box.generic.type = MTYPE_SPINCONTROL;
		s_no_mines_box.generic.x	= 0;
		s_no_mines_box.generic.y	= y += 10;
		s_no_mines_box.generic.name	= "remove mines";
		s_no_mines_box.generic.callback = DMFlagCallback;
		s_no_mines_box.itemnames = yes_no_names;
		s_no_mines_box.curvalue = ( dmflags & DF_NO_MINES ) != 0;

		s_no_nukes_box.generic.type = MTYPE_SPINCONTROL;
		s_no_nukes_box.generic.x	= 0;
		s_no_nukes_box.generic.y	= y += 10;
		s_no_nukes_box.generic.name	= "remove nukes";
		s_no_nukes_box.generic.callback = DMFlagCallback;
		s_no_nukes_box.itemnames = yes_no_names;
		s_no_nukes_box.curvalue = ( dmflags & DF_NO_NUKES ) != 0;

		s_stack_double_box.generic.type = MTYPE_SPINCONTROL;
		s_stack_double_box.generic.x	= 0;
		s_stack_double_box.generic.y	= y += 10;
		s_stack_double_box.generic.name	= "2x/4x stacking off";
		s_stack_double_box.generic.callback = DMFlagCallback;
		s_stack_double_box.itemnames = yes_no_names;
		s_stack_double_box.curvalue = ( dmflags & DF_NO_STACK_DOUBLE ) != 0;

		s_no_spheres_box.generic.type = MTYPE_SPINCONTROL;
		s_no_spheres_box.generic.x	= 0;
		s_no_spheres_box.generic.y	= y += 10;
		s_no_spheres_box.generic.name	= "remove spheres";
		s_no_spheres_box.generic.callback = DMFlagCallback;
		s_no_spheres_box.itemnames = yes_no_names;
		s_no_spheres_box.curvalue = ( dmflags & DF_NO_SPHERES ) != 0;

	}
//ROGUE
//============

	Menu_AddItem( &s_dmoptions_menu, &s_falls_box );
	Menu_AddItem( &s_dmoptions_menu, &s_weapons_stay_box );
	Menu_AddItem( &s_dmoptions_menu, &s_instant_powerups_box );
	Menu_AddItem( &s_dmoptions_menu, &s_powerups_box );
	Menu_AddItem( &s_dmoptions_menu, &s_health_box );
	Menu_AddItem( &s_dmoptions_menu, &s_armor_box );
	Menu_AddItem( &s_dmoptions_menu, &s_spawn_farthest_box );
	Menu_AddItem( &s_dmoptions_menu, &s_samelevel_box );
	Menu_AddItem( &s_dmoptions_menu, &s_force_respawn_box );
	Menu_AddItem( &s_dmoptions_menu, &s_teamplay_box );
	Menu_AddItem( &s_dmoptions_menu, &s_allow_exit_box );
	Menu_AddItem( &s_dmoptions_menu, &s_infinite_ammo_box );
	Menu_AddItem( &s_dmoptions_menu, &s_fixed_fov_box );
	Menu_AddItem( &s_dmoptions_menu, &s_quad_drop_box );
	Menu_AddItem( &s_dmoptions_menu, &s_friendlyfire_box );

//=======
//ROGUE
	if(Developer_searchpath(2) == 2)
	{
		Menu_AddItem( &s_dmoptions_menu, &s_no_mines_box );
		Menu_AddItem( &s_dmoptions_menu, &s_no_nukes_box );
		Menu_AddItem( &s_dmoptions_menu, &s_stack_double_box );
		Menu_AddItem( &s_dmoptions_menu, &s_no_spheres_box );
	}
//ROGUE
//=======

	Menu_Center( &s_dmoptions_menu );

	// set the original dmflags statusbar
	DMFlagCallback( 0 );
	Menu_SetStatusBar( &s_dmoptions_menu, dmoptions_statusbar );
}

void DMOptions_MenuDraw(void)
{
	Menu_Draw( &s_dmoptions_menu );
}

const char *DMOptions_MenuKey( int key )
{
	return Default_MenuKey( &s_dmoptions_menu, key );
}

void M_Menu_DMOptions_f (void)
{
	DMOptions_MenuInit();
	M_PushMenu( DMOptions_MenuDraw, DMOptions_MenuKey );
}

/*
=============================================================================

NET OPTIONS MENU (BDGN 3.37)
connect speed, packets per second and all the download options

=============================================================================
*/
static menuframework_s s_downloadoptions_menu;

static menuseparator_s	s_download_title;
static menulist_s	s_allow_download_box;
static menulist_s	s_allow_download_maps_box;
static menulist_s	s_allow_download_models_box;
static menulist_s	s_allow_download_players_box;
static menulist_s	s_allow_download_sounds_box;

static void DownloadCallback( void *self )
{
	menulist_s *f = ( menulist_s * ) self;

	if (f == &s_allow_download_box)
	{
		Cvar_SetValue("allow_download", f->curvalue);
	}

	else if (f == &s_allow_download_maps_box)
	{
		Cvar_SetValue("allow_download_maps", f->curvalue);
	}

	else if (f == &s_allow_download_models_box)
	{
		Cvar_SetValue("allow_download_models", f->curvalue);
	}

	else if (f == &s_allow_download_players_box)
	{
		Cvar_SetValue("allow_download_players", f->curvalue);
	}

	else if (f == &s_allow_download_sounds_box)
	{
		Cvar_SetValue("allow_download_sounds", f->curvalue);
	}
}

void DownloadOptions_MenuInit( void )
{
	static const char *yes_no_names[] =
	{
		"no", "yes", 0
	};
	static const char *rate_items[] = { "28.8 Modem", "33.6 Modem", "Single ISDN", "Dual ISDN/Cable", "T1/LAN", "User defined", 0 };
	static const char *packets_items[] = { "every frame", "60", "40", "30", "20", 0 };
	int rate = (int)Cvar_VariableValue( "rate" );
	int ri;
	int y = 0;

	s_downloadoptions_menu.x = viddef.width * 0.50;
	s_downloadoptions_menu.nitems = 0;

	s_download_title.generic.type = MTYPE_SEPARATOR;
	s_download_title.generic.name = "Net Options";
	s_download_title.generic.x    = 48;
	s_download_title.generic.y	 = y;

	for ( ri = 0 ; ri < 5 ; ri++ )
		if ( rate == opt_rate_table[ri] )
			break;				/* ri == 5 = "User defined" */
	s_options_rate_list.generic.type = MTYPE_SPINCONTROL;
	s_options_rate_list.generic.x = 0;
	s_options_rate_list.generic.y = y += 20;
	s_options_rate_list.generic.name = "connect speed";
	s_options_rate_list.generic.callback = OptRateFunc;
	s_options_rate_list.itemnames = rate_items;
	s_options_rate_list.curvalue = ri;

	s_options_packets_list.generic.type = MTYPE_SPINCONTROL;
	s_options_packets_list.generic.x = 0;
	s_options_packets_list.generic.y = y += 10;
	s_options_packets_list.generic.name = "packets per second";
	s_options_packets_list.generic.callback = OptPacketsFunc;
	s_options_packets_list.itemnames = packets_items;
	s_options_packets_list.curvalue = Opt_NearestIndex( opt_packets_table, 5, (int)Cvar_VariableValue( "cl_maxpackets" ) );

	s_allow_download_box.generic.type = MTYPE_SPINCONTROL;
	s_allow_download_box.generic.x	= 0;
	s_allow_download_box.generic.y	= y += 20;
	s_allow_download_box.generic.name	= "allow downloading";
	s_allow_download_box.generic.callback = DownloadCallback;
	s_allow_download_box.itemnames = yes_no_names;
	s_allow_download_box.curvalue = (Cvar_VariableValue("allow_download") != 0);

	s_allow_download_maps_box.generic.type = MTYPE_SPINCONTROL;
	s_allow_download_maps_box.generic.x	= 0;
	s_allow_download_maps_box.generic.y	= y += 20;
	s_allow_download_maps_box.generic.name	= "maps";
	s_allow_download_maps_box.generic.callback = DownloadCallback;
	s_allow_download_maps_box.itemnames = yes_no_names;
	s_allow_download_maps_box.curvalue = (Cvar_VariableValue("allow_download_maps") != 0);

	s_allow_download_players_box.generic.type = MTYPE_SPINCONTROL;
	s_allow_download_players_box.generic.x	= 0;
	s_allow_download_players_box.generic.y	= y += 10;
	s_allow_download_players_box.generic.name	= "player models/skins";
	s_allow_download_players_box.generic.callback = DownloadCallback;
	s_allow_download_players_box.itemnames = yes_no_names;
	s_allow_download_players_box.curvalue = (Cvar_VariableValue("allow_download_players") != 0);

	s_allow_download_models_box.generic.type = MTYPE_SPINCONTROL;
	s_allow_download_models_box.generic.x	= 0;
	s_allow_download_models_box.generic.y	= y += 10;
	s_allow_download_models_box.generic.name	= "models";
	s_allow_download_models_box.generic.callback = DownloadCallback;
	s_allow_download_models_box.itemnames = yes_no_names;
	s_allow_download_models_box.curvalue = (Cvar_VariableValue("allow_download_models") != 0);

	s_allow_download_sounds_box.generic.type = MTYPE_SPINCONTROL;
	s_allow_download_sounds_box.generic.x	= 0;
	s_allow_download_sounds_box.generic.y	= y += 10;
	s_allow_download_sounds_box.generic.name	= "sounds";
	s_allow_download_sounds_box.generic.callback = DownloadCallback;
	s_allow_download_sounds_box.itemnames = yes_no_names;
	s_allow_download_sounds_box.curvalue = (Cvar_VariableValue("allow_download_sounds") != 0);

	Menu_AddItem( &s_downloadoptions_menu, &s_download_title );
	Menu_AddItem( &s_downloadoptions_menu, &s_options_rate_list );
	Menu_AddItem( &s_downloadoptions_menu, &s_options_packets_list );
	Menu_AddItem( &s_downloadoptions_menu, &s_allow_download_box );
	Menu_AddItem( &s_downloadoptions_menu, &s_allow_download_maps_box );
	Menu_AddItem( &s_downloadoptions_menu, &s_allow_download_players_box );
	Menu_AddItem( &s_downloadoptions_menu, &s_allow_download_models_box );
	Menu_AddItem( &s_downloadoptions_menu, &s_allow_download_sounds_box );

	Menu_Center( &s_downloadoptions_menu );

	// skip over title
	if (s_downloadoptions_menu.cursor == 0)
		s_downloadoptions_menu.cursor = 1;
}

void DownloadOptions_MenuDraw(void)
{
	Menu_Draw( &s_downloadoptions_menu );
}

const char *DownloadOptions_MenuKey( int key )
{
	return Default_MenuKey( &s_downloadoptions_menu, key );
}

void M_Menu_DownloadOptions_f (void)
{
	DownloadOptions_MenuInit();
	M_PushMenu( DownloadOptions_MenuDraw, DownloadOptions_MenuKey );
}
/*
=============================================================================

ADDRESS BOOK MENU

=============================================================================
*/
#define NUM_ADDRESSBOOK_ENTRIES 9

static menuframework_s	s_addressbook_menu;
static menufield_s		s_addressbook_fields[NUM_ADDRESSBOOK_ENTRIES];

void AddressBook_MenuInit( void )
{
	int i;

	s_addressbook_menu.x = viddef.width / 2 - 142;
	s_addressbook_menu.y = viddef.height / 2 - 58;
	s_addressbook_menu.nitems = 0;

	for ( i = 0; i < NUM_ADDRESSBOOK_ENTRIES; i++ )
	{
		cvar_t *adr;
		char buffer[20];

		Com_sprintf( buffer, sizeof( buffer ), "adr%d", i );

		adr = Cvar_Get( buffer, "", CVAR_ARCHIVE );

		s_addressbook_fields[i].generic.type = MTYPE_FIELD;
		s_addressbook_fields[i].generic.name = 0;
		s_addressbook_fields[i].generic.callback = 0;
		s_addressbook_fields[i].generic.x		= 0;
		s_addressbook_fields[i].generic.y		= i * 18 + 0;
		s_addressbook_fields[i].generic.localdata[0] = i;
		s_addressbook_fields[i].cursor			= 0;
		s_addressbook_fields[i].length			= 60;
		s_addressbook_fields[i].visible_length	= 30;

		strcpy( s_addressbook_fields[i].buffer, adr->string );

		Menu_AddItem( &s_addressbook_menu, &s_addressbook_fields[i] );
	}
}

const char *AddressBook_MenuKey( int key )
{
	if ( key == K_ESCAPE )
	{
		int index;
		char buffer[20];

		for ( index = 0; index < NUM_ADDRESSBOOK_ENTRIES; index++ )
		{
			Com_sprintf( buffer, sizeof( buffer ), "adr%d", index );
			Cvar_Set( buffer, s_addressbook_fields[index].buffer );
		}
	}
	return Default_MenuKey( &s_addressbook_menu, key );
}

void AddressBook_MenuDraw(void)
{
	M_Banner( "m_banner_addressbook" );
	Menu_Draw( &s_addressbook_menu );
}

void M_Menu_AddressBook_f(void)
{
	AddressBook_MenuInit();
	M_PushMenu( AddressBook_MenuDraw, AddressBook_MenuKey );
}

/*
=============================================================================

PLAYER CONFIG MENU

=============================================================================
*/
static menuframework_s	s_player_config_menu;
static menufield_s		s_player_name_field;
static menulist_s		s_player_model_box;
static menulist_s		s_player_skin_box;
static menulist_s		s_player_handedness_box;
static menuseparator_s	s_player_skin_title;
static menuseparator_s	s_player_model_title;
static menuseparator_s	s_player_hand_title;

#define MAX_DISPLAYNAME 16
#define MAX_PLAYERMODELS 1024

typedef struct
{
	int		nskins;
	char	**skindisplaynames;
	char	displayname[MAX_DISPLAYNAME];
	char	directory[MAX_QPATH];
} playermodelinfo_s;

static playermodelinfo_s s_pmi[MAX_PLAYERMODELS];
static char *s_pmnames[MAX_PLAYERMODELS];
static int s_numplayermodels;


static void HandednessCallback( void *unused )
{
	Cvar_SetValue( "hand", s_player_handedness_box.curvalue );
}

static void ModelCallback( void *unused )
{
	s_player_skin_box.itemnames = s_pmi[s_player_model_box.curvalue].skindisplaynames;
	s_player_skin_box.curvalue = 0;
}

static void FreeFileList( char **list, int n )
{
	int i;

	for ( i = 0; i < n; i++ )
	{
		if ( list[i] )
		{
			free( list[i] );
			list[i] = 0;
		}
	}
	free( list );
}

static qboolean IconOfSkinExists( char *skin, char **pcxfiles, int npcxfiles )
{
	int i;
	char scratch[1024];

	strcpy( scratch, skin );
	*strrchr( scratch, '.' ) = 0;
	strcat( scratch, "_i.pcx" );

	for ( i = 0; i < npcxfiles; i++ )
	{
		if ( strcmp( pcxfiles[i], scratch ) == 0 )
			return true;
	}

	return false;
}

static qboolean PlayerConfig_ScanDirectories( void )
{
	char findname[1024];
	char scratch[1024];
	int ndirs = 0, npms = 0;
	char **dirnames;
	char *path = NULL;
	int i;

	extern char **FS_ListFiles( char *, int *, unsigned, unsigned );

	s_numplayermodels = 0;

	/*
	** get a list of directories
	*/
	do 
	{
		path = FS_NextPath( path );
		Com_sprintf( findname, sizeof(findname), "%s/players/*.*", path );

		if ( ( dirnames = FS_ListFiles( findname, &ndirs, SFF_SUBDIR, 0 ) ) != 0 )
			break;
	} while ( path );

	if ( !dirnames )
		return false;

	/*
	** go through the subdirectories
	*/
	npms = ndirs;
	if ( npms > MAX_PLAYERMODELS )
		npms = MAX_PLAYERMODELS;

	for ( i = 0; i < npms; i++ )
	{
		int k, s;
		char *a, *b, *c;
		char **pcxnames;
		char **skinnames;
		int npcxfiles;
		int nskins = 0;

		if ( dirnames[i] == 0 )
			continue;

		// verify the existence of tris.md2
		strcpy( scratch, dirnames[i] );
		strcat( scratch, "/tris.md2" );
		if ( !Sys_FindFirst( scratch, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM ) )
		{
			free( dirnames[i] );
			dirnames[i] = 0;
			Sys_FindClose();
			continue;
		}
		Sys_FindClose();

		// verify the existence of at least one pcx skin
		strcpy( scratch, dirnames[i] );
		strcat( scratch, "/*.pcx" );
		pcxnames = FS_ListFiles( scratch, &npcxfiles, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM );

		if ( !pcxnames )
		{
			free( dirnames[i] );
			dirnames[i] = 0;
			continue;
		}

		// count valid skins, which consist of a skin with a matching "_i" icon
		for ( k = 0; k < npcxfiles-1; k++ )
		{
			if ( !strstr( pcxnames[k], "_i.pcx" ) )
			{
				if ( IconOfSkinExists( pcxnames[k], pcxnames, npcxfiles - 1 ) )
				{
					nskins++;
				}
			}
		}
		if ( !nskins )
			continue;

		skinnames = malloc( sizeof( char * ) * ( nskins + 1 ) );
		memset( skinnames, 0, sizeof( char * ) * ( nskins + 1 ) );

		// copy the valid skins
		for ( s = 0, k = 0; k < npcxfiles-1; k++ )
		{
			char *a, *b, *c;

			if ( !strstr( pcxnames[k], "_i.pcx" ) )
			{
				if ( IconOfSkinExists( pcxnames[k], pcxnames, npcxfiles - 1 ) )
				{
					a = strrchr( pcxnames[k], '/' );
					b = strrchr( pcxnames[k], '\\' );

					if ( a > b )
						c = a;
					else
						c = b;

					strcpy( scratch, c + 1 );

					if ( strrchr( scratch, '.' ) )
						*strrchr( scratch, '.' ) = 0;

					skinnames[s] = strdup( scratch );
					s++;
				}
			}
		}

		// at this point we have a valid player model
		s_pmi[s_numplayermodels].nskins = nskins;
		s_pmi[s_numplayermodels].skindisplaynames = skinnames;

		// make short name for the model
		a = strrchr( dirnames[i], '/' );
		b = strrchr( dirnames[i], '\\' );

		if ( a > b )
			c = a;
		else
			c = b;

		strncpy( s_pmi[s_numplayermodels].displayname, c + 1, MAX_DISPLAYNAME-1 );
		strcpy( s_pmi[s_numplayermodels].directory, c + 1 );

		FreeFileList( pcxnames, npcxfiles );

		s_numplayermodels++;
	}
	if ( dirnames )
		FreeFileList( dirnames, ndirs );
	return s_numplayermodels > 0 ? true : false;
}

static int pmicmpfnc( const void *_a, const void *_b )
{
	const playermodelinfo_s *a = ( const playermodelinfo_s * ) _a;
	const playermodelinfo_s *b = ( const playermodelinfo_s * ) _b;

	/*
	** sort by male, female, then alphabetical
	*/
	if ( strcmp( a->directory, "male" ) == 0 )
		return -1;
	else if ( strcmp( b->directory, "male" ) == 0 )
		return 1;

	if ( strcmp( a->directory, "female" ) == 0 )
		return -1;
	else if ( strcmp( b->directory, "female" ) == 0 )
		return 1;

	return strcmp( a->directory, b->directory );
}


qboolean PlayerConfig_MenuInit( void )
{
	extern cvar_t *name;
	extern cvar_t *team;
	extern cvar_t *skin;
	char currentdirectory[1024];
	char currentskin[1024];
	int i = 0;

	int currentdirectoryindex = 0;
	int currentskinindex = 0;

	cvar_t *hand = Cvar_Get( "hand", "0", CVAR_USERINFO | CVAR_ARCHIVE );

	static const char *handedness[] = { "right hand", "left hand", "hidden", 0 };

	PlayerConfig_ScanDirectories();

	if (s_numplayermodels == 0)
		return false;

	if ( hand->value < 0 || hand->value > 2 )
		Cvar_SetValue( "hand", 0 );

	strcpy( currentdirectory, skin->string );

	if ( strchr( currentdirectory, '/' ) )
	{
		strcpy( currentskin, strchr( currentdirectory, '/' ) + 1 );
		*strchr( currentdirectory, '/' ) = 0;
	}
	else if ( strchr( currentdirectory, '\\' ) )
	{
		strcpy( currentskin, strchr( currentdirectory, '\\' ) + 1 );
		*strchr( currentdirectory, '\\' ) = 0;
	}
	else
	{
		strcpy( currentdirectory, "male" );
		strcpy( currentskin, "grunt" );
	}

	qsort( s_pmi, s_numplayermodels, sizeof( s_pmi[0] ), pmicmpfnc );

	memset( s_pmnames, 0, sizeof( s_pmnames ) );
	for ( i = 0; i < s_numplayermodels; i++ )
	{
		s_pmnames[i] = s_pmi[i].displayname;
		if ( Q_stricmp( s_pmi[i].directory, currentdirectory ) == 0 )
		{
			int j;

			currentdirectoryindex = i;

			for ( j = 0; j < s_pmi[i].nskins; j++ )
			{
				if ( Q_stricmp( s_pmi[i].skindisplaynames[j], currentskin ) == 0 )
				{
					currentskinindex = j;
					break;
				}
			}
		}
	}

	s_player_config_menu.x = viddef.width / 2 - 95; 
	s_player_config_menu.y = viddef.height / 2 - 97;
	s_player_config_menu.nitems = 0;

	s_player_name_field.generic.type = MTYPE_FIELD;
	s_player_name_field.generic.name = "name";
	s_player_name_field.generic.callback = 0;
	s_player_name_field.generic.x		= 0;
	s_player_name_field.generic.y		= 0;
	s_player_name_field.length	= 20;
	s_player_name_field.visible_length = 20;
	strcpy( s_player_name_field.buffer, name->string );
	s_player_name_field.cursor = strlen( name->string );

	s_player_model_title.generic.type = MTYPE_SEPARATOR;
	s_player_model_title.generic.name = "model";
	s_player_model_title.generic.x    = -8;
	s_player_model_title.generic.y	 = 60;

	s_player_model_box.generic.type = MTYPE_SPINCONTROL;
	s_player_model_box.generic.x	= -56;
	s_player_model_box.generic.y	= 70;
	s_player_model_box.generic.callback = ModelCallback;
	s_player_model_box.generic.cursor_offset = -48;
	s_player_model_box.curvalue = currentdirectoryindex;
	s_player_model_box.itemnames = s_pmnames;

	s_player_skin_title.generic.type = MTYPE_SEPARATOR;
	s_player_skin_title.generic.name = "skin";
	s_player_skin_title.generic.x    = -16;
	s_player_skin_title.generic.y	 = 84;

	s_player_skin_box.generic.type = MTYPE_SPINCONTROL;
	s_player_skin_box.generic.x	= -56;
	s_player_skin_box.generic.y	= 94;
	s_player_skin_box.generic.name	= 0;
	s_player_skin_box.generic.callback = 0;
	s_player_skin_box.generic.cursor_offset = -48;
	s_player_skin_box.curvalue = currentskinindex;
	s_player_skin_box.itemnames = s_pmi[currentdirectoryindex].skindisplaynames;

	s_player_hand_title.generic.type = MTYPE_SEPARATOR;
//	s_player_hand_title.generic.name = "handedness";
	s_player_hand_title.generic.name = "weapon";
//	s_player_hand_title.generic.x    = 32;
	s_player_hand_title.generic.x    = 0;
	s_player_hand_title.generic.y	 = 108;

	s_player_handedness_box.generic.type = MTYPE_SPINCONTROL;
	s_player_handedness_box.generic.x	= -56;
	s_player_handedness_box.generic.y	= 118;
	s_player_handedness_box.generic.name	= 0;
	s_player_handedness_box.generic.cursor_offset = -48;
	s_player_handedness_box.generic.callback = HandednessCallback;
	s_player_handedness_box.curvalue = Cvar_VariableValue( "hand" );
	s_player_handedness_box.itemnames = handedness;

	Menu_AddItem( &s_player_config_menu, &s_player_name_field );
	Menu_AddItem( &s_player_config_menu, &s_player_model_title );
	Menu_AddItem( &s_player_config_menu, &s_player_model_box );
	if ( s_player_skin_box.itemnames )
	{
		Menu_AddItem( &s_player_config_menu, &s_player_skin_title );
		Menu_AddItem( &s_player_config_menu, &s_player_skin_box );
	}
	Menu_AddItem( &s_player_config_menu, &s_player_hand_title );
	Menu_AddItem( &s_player_config_menu, &s_player_handedness_box );

	return true;
}

void PlayerConfig_MenuDraw( void )
{
	extern float CalcFov( float fov_x, float w, float h );
	refdef_t refdef;
	char scratch[MAX_QPATH];

	memset( &refdef, 0, sizeof( refdef ) );

	refdef.x = viddef.width / 2;
	refdef.y = viddef.height / 2 - 72;
	refdef.width = 144;
	refdef.height = 168;
	refdef.fov_x = 40;
	refdef.fov_y = CalcFov( refdef.fov_x, refdef.width, refdef.height );
	refdef.time = cls.realtime*0.001;

	if ( s_pmi[s_player_model_box.curvalue].skindisplaynames )
	{
		static int yaw;
		int maxframe = 29;
		entity_t entity;

		memset( &entity, 0, sizeof( entity ) );

		Com_sprintf( scratch, sizeof( scratch ), "players/%s/tris.md2", s_pmi[s_player_model_box.curvalue].directory );
		entity.model = re.RegisterModel( scratch );
		Com_sprintf( scratch, sizeof( scratch ), "players/%s/%s.pcx", s_pmi[s_player_model_box.curvalue].directory, s_pmi[s_player_model_box.curvalue].skindisplaynames[s_player_skin_box.curvalue] );
		entity.skin = re.RegisterSkin( scratch );
		entity.flags = RF_FULLBRIGHT;
		entity.origin[0] = 80;
		entity.origin[1] = 0;
		entity.origin[2] = 0;
		VectorCopy( entity.origin, entity.oldorigin );
		entity.frame = 0;
		entity.oldframe = 0;
		entity.backlerp = 0.0;
		entity.angles[1] = yaw++;
		if ( ++yaw > 360 )
			yaw -= 360;

		refdef.areabits = 0;
		refdef.num_entities = 1;
		refdef.entities = &entity;
		refdef.lightstyles = 0;
		refdef.rdflags = RDF_NOWORLDMODEL;

		Menu_Draw( &s_player_config_menu );

		M_DrawTextBox( ( refdef.x ) * ( 320.0F / viddef.width ) - 8, ( viddef.height / 2 ) * ( 240.0F / viddef.height) - 77, refdef.width / 8, refdef.height / 8 );
		refdef.height += 4;

		{	// BDGN 3.50: refdef stays in 2D (virtual) coordinates for the text box and icon; the 3D view is real pixels
			refdef_t view = refdef;
			VID_RectToRealPixels (&view.x, &view.y, &view.width, &view.height);
			re.RenderFrame( &view );
		}

		Com_sprintf( scratch, sizeof( scratch ), "/players/%s/%s_i.pcx", 
			s_pmi[s_player_model_box.curvalue].directory,
			s_pmi[s_player_model_box.curvalue].skindisplaynames[s_player_skin_box.curvalue] );
		re.DrawPic( s_player_config_menu.x - 40, refdef.y, scratch );
	}
}

const char *PlayerConfig_MenuKey (int key)
{
	int i;

	if ( key == K_ESCAPE )
	{
		char scratch[1024];

		Cvar_Set( "name", s_player_name_field.buffer );

		Com_sprintf( scratch, sizeof( scratch ), "%s/%s", 
			s_pmi[s_player_model_box.curvalue].directory, 
			s_pmi[s_player_model_box.curvalue].skindisplaynames[s_player_skin_box.curvalue] );

		Cvar_Set( "skin", scratch );

		for ( i = 0; i < s_numplayermodels; i++ )
		{
			int j;

			for ( j = 0; j < s_pmi[i].nskins; j++ )
			{
				if ( s_pmi[i].skindisplaynames[j] )
					free( s_pmi[i].skindisplaynames[j] );
				s_pmi[i].skindisplaynames[j] = 0;
			}
			free( s_pmi[i].skindisplaynames );
			s_pmi[i].skindisplaynames = 0;
			s_pmi[i].nskins = 0;
		}
	}
	return Default_MenuKey( &s_player_config_menu, key );
}


void M_Menu_PlayerConfig_f (void)
{
	if (!PlayerConfig_MenuInit())
	{
		Menu_SetStatusBar( &s_multiplayer_menu, "No valid player models found" );
		return;
	}
	Menu_SetStatusBar( &s_multiplayer_menu, NULL );
	M_PushMenu( PlayerConfig_MenuDraw, PlayerConfig_MenuKey );
}


/*
=======================================================================

GALLERY MENU

=======================================================================
*/
#if 0
void M_Menu_Gallery_f( void )
{
	extern void Gallery_MenuDraw( void );
	extern const char *Gallery_MenuKey( int key );

	M_PushMenu( Gallery_MenuDraw, Gallery_MenuKey );
}
#endif

/*
=======================================================================

QUIT MENU

=======================================================================
*/

const char *M_Quit_Key (int key)
{
	switch (key)
	{
	case K_ESCAPE:
	case 'n':
	case 'N':
		M_PopMenu ();
		break;

	case 'Y':
	case 'y':
		cls.key_dest = key_console;
		CL_Quit_f ();
		break;

	default:
		break;
	}

	return NULL;

}


void M_Quit_Draw (void)
{
	int		w, h;

	re.DrawGetPicSize (&w, &h, "quit");
	re.DrawPic ( (viddef.width-w)/2, (viddef.height-h)/2, "quit");
}


void M_Menu_Quit_f (void)
{
	M_PushMenu (M_Quit_Draw, M_Quit_Key);
}



//=============================================================================
/* Menu Subsystem */


/*
=================
M_Init
=================
*/
void M_Init (void)
{
	Cmd_AddCommand ("menu_main", M_Menu_Main_f);
	Cmd_AddCommand ("menu_game", M_Menu_Game_f);
		Cmd_AddCommand ("menu_loadgame", M_Menu_LoadGame_f);
		Cmd_AddCommand ("menu_savegame", M_Menu_SaveGame_f);
		Cmd_AddCommand ("menu_joinserver", M_Menu_JoinServer_f);
			Cmd_AddCommand ("menu_addressbook", M_Menu_AddressBook_f);
		Cmd_AddCommand ("menu_startserver", M_Menu_StartServer_f);
			Cmd_AddCommand ("menu_dmoptions", M_Menu_DMOptions_f);
		Cmd_AddCommand ("menu_playerconfig", M_Menu_PlayerConfig_f);
			Cmd_AddCommand ("menu_downloadoptions", M_Menu_DownloadOptions_f);
			Cmd_AddCommand ("menu_netoptions", M_Menu_DownloadOptions_f);
		Cmd_AddCommand ("menu_credits", M_Menu_Credits_f );
	Cmd_AddCommand ("menu_multiplayer", M_Menu_Multiplayer_f );
	Cmd_AddCommand ("menu_video", M_Menu_Video_f);
	Cmd_AddCommand ("menu_options", M_Menu_Options_f);
	Cmd_AddCommand ("menu_speed", M_Menu_Speed_f);
	Cmd_AddCommand ("menu_tweaks_graphics", M_Menu_TweaksGraphics_f);
	Cmd_AddCommand ("menu_tweaks_effects", M_Menu_TweaksEffects_f);
	Cmd_AddCommand ("menu_testing", M_Menu_TweaksTesting_f);
		Cmd_AddCommand ("menu_keys", M_Menu_Keys_f);
	Cmd_AddCommand ("menu_quit", M_Menu_Quit_f);
}


/*
=================
M_Draw
=================
*/
void M_Draw (void)
{
	if (cls.key_dest != key_menu)
		return;

	// repaint everything next frame
	SCR_DirtyScreen ();

	// dim everything behind it down
	if (cl.cinematictime > 0)
		re.DrawFill (0,0,viddef.width, viddef.height, 0);
	else
		re.DrawFadeScreen ();

	m_drawfunc ();

	// delay playing the enter sound until after the
	// menu has been drawn, to avoid delay while
	// caching images
	if (m_entersound)
	{
		S_StartLocalSound( menu_in_sound );
		m_entersound = false;
	}
}


/*
=================
M_Keydown
=================
*/
void M_Keydown (int key)
{
	const char *s;

	if (m_keyfunc)
		if ( ( s = m_keyfunc( key ) ) != 0 )
			S_StartLocalSound( ( char * ) s );
}


