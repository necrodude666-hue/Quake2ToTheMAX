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

#include "g_local.h"

#define Function(f) {#f, f}

mmove_t mmove_reloc;

/* BDGN 3.59z (the idea of Yamagi Quake II): function and monster move pointers are saved as names, looked up in
   tables of every game function and move (one per file, g_savenames.h), so a save game still loads after gamex86.dll
   is rebuilt. id's 3.21 saved the distance to InitGame and refused any save from a build of another day (__DATE__).
   A pointer missing from the tables (none known) is saved as "@" + its old offset: loadable by the same build only. */
void InitGame (void);
#include "g_savenames.h"
#define G_SAVE_MAGIC "BDGN-SAVE-2"
#define G_SAVE_MAXNAMES 1600
static g_savefunc_t g_fn_byname[G_SAVE_MAXNAMES], g_fn_byptr[G_SAVE_MAXNAMES];
static g_savemove_t g_mv_byname[G_SAVE_MAXNAMES/2], g_mv_byptr[G_SAVE_MAXNAMES/2];
static int g_fn_count, g_mv_count, g_names_ready;
void G_AddSaveNames (const g_savefunc_t *funcs, const g_savemove_t *moves)
{
	for ( ; funcs && funcs->name; funcs++)
		if (g_fn_count < G_SAVE_MAXNAMES) { g_fn_byname[g_fn_count] = *funcs; g_fn_byptr[g_fn_count++] = *funcs; }
	for ( ; moves && moves->name; moves++)
		if (g_mv_count < G_SAVE_MAXNAMES/2) { g_mv_byname[g_mv_count] = *moves; g_mv_byptr[g_mv_count++] = *moves; }
}
static int G_CmpFnName (const void *a, const void *b) { return strcmp (((g_savefunc_t *)a)->name, ((g_savefunc_t *)b)->name); }
static int G_CmpFnPtr (const void *a, const void *b)
{
	size_t x = (size_t)((g_savefunc_t *)a)->func, y = (size_t)((g_savefunc_t *)b)->func;
	return x < y ? -1 : x > y;
}
static int G_CmpMvName (const void *a, const void *b) { return strcmp (((g_savemove_t *)a)->name, ((g_savemove_t *)b)->name); }
static int G_CmpMvPtr (const void *a, const void *b)
{
	size_t x = (size_t)((g_savemove_t *)a)->move, y = (size_t)((g_savemove_t *)b)->move;
	return x < y ? -1 : x > y;
}
static void G_SaveNamesInit (void)
{
	if (g_names_ready) return;
	g_fn_count = g_mv_count = 0;
	G_SaveNamesAll ();
	qsort (g_fn_byname, g_fn_count, sizeof(g_fn_byname[0]), G_CmpFnName);
	qsort (g_fn_byptr, g_fn_count, sizeof(g_fn_byptr[0]), G_CmpFnPtr);
	qsort (g_mv_byname, g_mv_count, sizeof(g_mv_byname[0]), G_CmpMvName);
	qsort (g_mv_byptr, g_mv_count, sizeof(g_mv_byptr[0]), G_CmpMvPtr);
	g_names_ready = 1;
}
/* the name of a function or move pointer (static buffer for the "@" fallback) */
static const char *G_SaveNameOf (void *ptr, int move)
{
	static char fallback[32];
	if (move) {
		g_savemove_t key, *r;
		key.move = (mmove_t *)ptr;
		r = (g_savemove_t *)bsearch (&key, g_mv_byptr, g_mv_count, sizeof(key), G_CmpMvPtr);
		if (r) return r->name;
		Com_sprintf (fallback, sizeof(fallback), "@m%d", (int)((byte *)ptr - (byte *)&mmove_reloc));
	} else {
		g_savefunc_t key, *r;
		key.func = (g_anyfunc_t)ptr;
		r = (g_savefunc_t *)bsearch (&key, g_fn_byptr, g_fn_count, sizeof(key), G_CmpFnPtr);
		if (r) return r->name;
		Com_sprintf (fallback, sizeof(fallback), "@f%d", (int)((byte *)ptr - (byte *)InitGame));
	}
	return fallback;
}
static void *G_SavePtrOf (const char *name, int move)
{
	if (name[0] == '@')
		return move ? (void *)((byte *)&mmove_reloc + atoi (name + 2)) : (void *)((byte *)InitGame + atoi (name + 2));
	if (move) {
		g_savemove_t key, *r;
		key.name = name;
		r = (g_savemove_t *)bsearch (&key, g_mv_byname, g_mv_count, sizeof(key), G_CmpMvName);
		if (r) return r->move;
	} else {
		g_savefunc_t key, *r;
		key.name = name;
		r = (g_savefunc_t *)bsearch (&key, g_fn_byname, g_fn_count, sizeof(key), G_CmpFnName);
		if (r) return (void *)r->func;
	}
	gi.error ("Save game: %s %s is not in this gamex86.dll", move ? "monster move" : "function", name);
	return NULL;
}

field_t fields[] = {
	{"classname", FOFS(classname), F_LSTRING},
	{"model", FOFS(model), F_LSTRING},
	{"spawnflags", FOFS(spawnflags), F_INT},
	{"speed", FOFS(speed), F_FLOAT},
	{"accel", FOFS(accel), F_FLOAT},
	{"decel", FOFS(decel), F_FLOAT},
	{"target", FOFS(target), F_LSTRING},
	{"targetname", FOFS(targetname), F_LSTRING},
	{"pathtarget", FOFS(pathtarget), F_LSTRING},
	{"deathtarget", FOFS(deathtarget), F_LSTRING},
	{"killtarget", FOFS(killtarget), F_LSTRING},
	{"combattarget", FOFS(combattarget), F_LSTRING},
	{"message", FOFS(message), F_LSTRING},
	{"team", FOFS(team), F_LSTRING},
	{"wait", FOFS(wait), F_FLOAT},
	{"delay", FOFS(delay), F_FLOAT},
	{"random", FOFS(random), F_FLOAT},
	{"move_origin", FOFS(move_origin), F_VECTOR},
	{"move_angles", FOFS(move_angles), F_VECTOR},
	{"style", FOFS(style), F_INT},
	{"count", FOFS(count), F_INT},
	{"health", FOFS(health), F_INT},
	{"sounds", FOFS(sounds), F_INT},
	{"light", 0, F_IGNORE},
	{"dmg", FOFS(dmg), F_INT},
	{"mass", FOFS(mass), F_INT},
	{"volume", FOFS(volume), F_FLOAT},
	{"attenuation", FOFS(attenuation), F_FLOAT},
	{"map", FOFS(map), F_LSTRING},
	{"origin", FOFS(s.origin), F_VECTOR},
	{"angles", FOFS(s.angles), F_VECTOR},
	{"angle", FOFS(s.angles), F_ANGLEHACK},

	{"goalentity", FOFS(goalentity), F_EDICT, FFL_NOSPAWN},
	{"movetarget", FOFS(movetarget), F_EDICT, FFL_NOSPAWN},
	{"enemy", FOFS(enemy), F_EDICT, FFL_NOSPAWN},
	{"oldenemy", FOFS(oldenemy), F_EDICT, FFL_NOSPAWN},
	{"activator", FOFS(activator), F_EDICT, FFL_NOSPAWN},
	{"groundentity", FOFS(groundentity), F_EDICT, FFL_NOSPAWN},
	{"teamchain", FOFS(teamchain), F_EDICT, FFL_NOSPAWN},
	{"teammaster", FOFS(teammaster), F_EDICT, FFL_NOSPAWN},
	{"owner", FOFS(owner), F_EDICT, FFL_NOSPAWN},
	{"mynoise", FOFS(mynoise), F_EDICT, FFL_NOSPAWN},
	{"mynoise2", FOFS(mynoise2), F_EDICT, FFL_NOSPAWN},
	{"target_ent", FOFS(target_ent), F_EDICT, FFL_NOSPAWN},
	{"chain", FOFS(chain), F_EDICT, FFL_NOSPAWN},

	{"prethink", FOFS(prethink), F_FUNCTION, FFL_NOSPAWN},
	{"think", FOFS(think), F_FUNCTION, FFL_NOSPAWN},
	{"blocked", FOFS(blocked), F_FUNCTION, FFL_NOSPAWN},
	{"touch", FOFS(touch), F_FUNCTION, FFL_NOSPAWN},
	{"use", FOFS(use), F_FUNCTION, FFL_NOSPAWN},
	{"pain", FOFS(pain), F_FUNCTION, FFL_NOSPAWN},
	{"die", FOFS(die), F_FUNCTION, FFL_NOSPAWN},

	{"stand", FOFS(monsterinfo.stand), F_FUNCTION, FFL_NOSPAWN},
	{"idle", FOFS(monsterinfo.idle), F_FUNCTION, FFL_NOSPAWN},
	{"search", FOFS(monsterinfo.search), F_FUNCTION, FFL_NOSPAWN},
	{"walk", FOFS(monsterinfo.walk), F_FUNCTION, FFL_NOSPAWN},
	{"run", FOFS(monsterinfo.run), F_FUNCTION, FFL_NOSPAWN},
	{"dodge", FOFS(monsterinfo.dodge), F_FUNCTION, FFL_NOSPAWN},
	{"attack", FOFS(monsterinfo.attack), F_FUNCTION, FFL_NOSPAWN},
	{"melee", FOFS(monsterinfo.melee), F_FUNCTION, FFL_NOSPAWN},
	{"sight", FOFS(monsterinfo.sight), F_FUNCTION, FFL_NOSPAWN},
	{"checkattack", FOFS(monsterinfo.checkattack), F_FUNCTION, FFL_NOSPAWN},
	{"currentmove", FOFS(monsterinfo.currentmove), F_MMOVE, FFL_NOSPAWN},

	{"endfunc", FOFS(moveinfo.endfunc), F_FUNCTION, FFL_NOSPAWN},

	// temp spawn vars -- only valid when the spawn function is called
	{"lip", STOFS(lip), F_INT, FFL_SPAWNTEMP},
	{"distance", STOFS(distance), F_INT, FFL_SPAWNTEMP},
	{"height", STOFS(height), F_INT, FFL_SPAWNTEMP},
	{"noise", STOFS(noise), F_LSTRING, FFL_SPAWNTEMP},
	{"pausetime", STOFS(pausetime), F_FLOAT, FFL_SPAWNTEMP},
	{"item", STOFS(item), F_LSTRING, FFL_SPAWNTEMP},

//need for item field in edict struct, FFL_SPAWNTEMP item will be skipped on saves
	{"item", FOFS(item), F_ITEM},

	{"gravity", STOFS(gravity), F_LSTRING, FFL_SPAWNTEMP},
	{"sky", STOFS(sky), F_LSTRING, FFL_SPAWNTEMP},
	{"skyrotate", STOFS(skyrotate), F_FLOAT, FFL_SPAWNTEMP},
	{"skyaxis", STOFS(skyaxis), F_VECTOR, FFL_SPAWNTEMP},
	{"minyaw", STOFS(minyaw), F_FLOAT, FFL_SPAWNTEMP},
	{"maxyaw", STOFS(maxyaw), F_FLOAT, FFL_SPAWNTEMP},
	{"minpitch", STOFS(minpitch), F_FLOAT, FFL_SPAWNTEMP},
	{"maxpitch", STOFS(maxpitch), F_FLOAT, FFL_SPAWNTEMP},
	{"nextmap", STOFS(nextmap), F_LSTRING, FFL_SPAWNTEMP},

	{0, 0, 0, 0}

};

field_t		levelfields[] =
{
	{"changemap", LLOFS(changemap), F_LSTRING},
                   
	{"sight_client", LLOFS(sight_client), F_EDICT},
	{"sight_entity", LLOFS(sight_entity), F_EDICT},
	{"sound_entity", LLOFS(sound_entity), F_EDICT},
	{"sound2_entity", LLOFS(sound2_entity), F_EDICT},

	{NULL, 0, F_INT}
};

field_t		clientfields[] =
{
	{"pers.weapon", CLOFS(pers.weapon), F_ITEM},
	{"pers.lastweapon", CLOFS(pers.lastweapon), F_ITEM},
	{"newweapon", CLOFS(newweapon), F_ITEM},

	{NULL, 0, F_INT}
};

/*
============
InitGame

This will be called when the dll is first loaded, which
only happens when a new game is started or a save game
is loaded.
============
*/
void InitGame (void)
{
	gi.dprintf ("==== InitGame ====\n");

	gun_x = gi.cvar ("gun_x", "0", 0);
	gun_y = gi.cvar ("gun_y", "0", 0);
	gun_z = gi.cvar ("gun_z", "0", 0);

	//FIXME: sv_ prefix is wrong for these
	sv_rollspeed = gi.cvar ("sv_rollspeed", "200", 0);
	sv_rollangle = gi.cvar ("sv_rollangle", "2", 0);
	sv_maxvelocity = gi.cvar ("sv_maxvelocity", "2000", 0);
	sv_gravity = gi.cvar ("sv_gravity", "800", 0);

	// noset vars
	dedicated = gi.cvar ("dedicated", "0", CVAR_NOSET);

	// latched vars
	sv_cheats = gi.cvar ("cheats", "0", CVAR_SERVERINFO|CVAR_LATCH);
	gi.cvar ("gamename", GAMEVERSION , CVAR_SERVERINFO | CVAR_LATCH);
	gi.cvar ("gamedate", __DATE__ , CVAR_SERVERINFO | CVAR_LATCH);

	maxclients = gi.cvar ("maxclients", "4", CVAR_SERVERINFO | CVAR_LATCH);
	maxspectators = gi.cvar ("maxspectators", "4", CVAR_SERVERINFO);
	deathmatch = gi.cvar ("deathmatch", "0", CVAR_LATCH);
	coop = gi.cvar ("coop", "0", CVAR_LATCH);
	skill = gi.cvar ("skill", "1", CVAR_LATCH);
	maxentities = gi.cvar ("maxentities", "1024", CVAR_LATCH);

	// change anytime vars
	dmflags = gi.cvar ("dmflags", "0", CVAR_SERVERINFO);
	fraglimit = gi.cvar ("fraglimit", "0", CVAR_SERVERINFO);
	timelimit = gi.cvar ("timelimit", "0", CVAR_SERVERINFO);
	password = gi.cvar ("password", "", CVAR_USERINFO);
	spectator_password = gi.cvar ("spectator_password", "", CVAR_USERINFO);
	needpass = gi.cvar ("needpass", "0", CVAR_SERVERINFO);
	filterban = gi.cvar ("filterban", "1", 0);

	g_select_empty = gi.cvar ("g_select_empty", "0", CVAR_ARCHIVE);
	g_gibmode = gi.cvar ("g_gibmode", "0", CVAR_ARCHIVE);
	g_noflies = gi.cvar ("g_noflies", "0", CVAR_ARCHIVE);

	run_pitch = gi.cvar ("run_pitch", "0.002", 0);
	run_roll = gi.cvar ("run_roll", "0.005", 0);
	bob_up  = gi.cvar ("bob_up", "0.005", 0);
	bob_pitch = gi.cvar ("bob_pitch", "0.002", 0);
	bob_roll = gi.cvar ("bob_roll", "0.002", 0);

	// flood control
	flood_msgs = gi.cvar ("flood_msgs", "4", 0);
	flood_persecond = gi.cvar ("flood_persecond", "4", 0);
	flood_waitdelay = gi.cvar ("flood_waitdelay", "10", 0);

	// dm map list
	sv_maplist = gi.cvar ("sv_maplist", "", 0);

	// items
	InitItems ();

	Com_sprintf (game.helpmessage1, sizeof(game.helpmessage1), "");

	Com_sprintf (game.helpmessage2, sizeof(game.helpmessage2), "");

	// initialize all entities for this game
	game.maxentities = maxentities->value;
	g_edicts =  gi.TagMalloc (game.maxentities * sizeof(g_edicts[0]), TAG_GAME);
	globals.edicts = g_edicts;
	globals.max_edicts = game.maxentities;

	// initialize all clients for this game
	game.maxclients = maxclients->value;
	game.clients = gi.TagMalloc (game.maxclients * sizeof(game.clients[0]), TAG_GAME);
	globals.num_edicts = game.maxclients+1;
}

//=========================================================

void WriteField1 (FILE *f, field_t *field, byte *base)
{
	void		*p;
	int			len;
	int			index;

	if (field->flags & FFL_SPAWNTEMP)
		return;

	p = (void *)(base + field->ofs);
	switch (field->type)
	{
	case F_INT:
	case F_FLOAT:
	case F_ANGLEHACK:
	case F_VECTOR:
	case F_IGNORE:
		break;

	case F_LSTRING:
	case F_GSTRING:
		if ( *(char **)p )
			len = strlen(*(char **)p) + 1;
		else
			len = 0;
		*(int *)p = len;
		break;
	case F_EDICT:
		if ( *(edict_t **)p == NULL)
			index = -1;
		else
			index = *(edict_t **)p - g_edicts;
		*(int *)p = index;
		break;
	case F_CLIENT:
		if ( *(gclient_t **)p == NULL)
			index = -1;
		else
			index = *(gclient_t **)p - game.clients;
		*(int *)p = index;
		break;
	case F_ITEM:
		if ( *(edict_t **)p == NULL)
			index = -1;
		else
			index = *(gitem_t **)p - itemlist;
		*(int *)p = index;
		break;

	/* BDGN 3.59z: names (WriteField2 writes the text) */
	case F_FUNCTION:
	case F_MMOVE:
		if (*(byte **)p == NULL)
			len = 0;
		else
			len = strlen (G_SaveNameOf (*(void **)p, field->type == F_MMOVE)) + 1;
		*(int *)p = len;
		break;

	default:
		gi.error ("WriteEdict: unknown field type");
	}
}


void WriteField2 (FILE *f, field_t *field, byte *base)
{
	int			len;
	void		*p;

	if (field->flags & FFL_SPAWNTEMP)
		return;

	p = (void *)(base + field->ofs);
	switch (field->type)
	{
	case F_LSTRING:
		if ( *(char **)p )
		{
			len = strlen(*(char **)p) + 1;
			fwrite (*(char **)p, len, 1, f);
		}
		break;
	case F_FUNCTION:	/* BDGN 3.59z */
	case F_MMOVE:
		if ( *(void **)p )
		{
			const char *name = G_SaveNameOf (*(void **)p, field->type == F_MMOVE);
			fwrite (name, strlen (name) + 1, 1, f);
		}
		break;
	}
}

void ReadField (FILE *f, field_t *field, byte *base)
{
	void		*p;
	int			len;
	int			index;

	if (field->flags & FFL_SPAWNTEMP)
		return;

	p = (void *)(base + field->ofs);
	switch (field->type)
	{
	case F_INT:
	case F_FLOAT:
	case F_ANGLEHACK:
	case F_VECTOR:
	case F_IGNORE:
		break;

	case F_LSTRING:
		len = *(int *)p;
		if (!len)
			*(char **)p = NULL;
		else
		{
			*(char **)p = gi.TagMalloc (len, TAG_LEVEL);
			fread (*(char **)p, len, 1, f);
		}
		break;
	case F_EDICT:
		index = *(int *)p;
		if ( index == -1 )
			*(edict_t **)p = NULL;
		else
			*(edict_t **)p = &g_edicts[index];
		break;
	case F_CLIENT:
		index = *(int *)p;
		if ( index == -1 )
			*(gclient_t **)p = NULL;
		else
			*(gclient_t **)p = &game.clients[index];
		break;
	case F_ITEM:
		index = *(int *)p;
		if ( index == -1 )
			*(gitem_t **)p = NULL;
		else
			*(gitem_t **)p = &itemlist[index];
		break;

	/* BDGN 3.59z: names */
	case F_FUNCTION:
	case F_MMOVE:
		len = *(int *)p;
		if (len <= 0)
			*(void **)p = NULL;
		else
		{
			char name[128];
			if (len > (int)sizeof(name))
				gi.error ("Save game: damaged (name of %i characters)", len);
			fread (name, len, 1, f);
			name[len - 1] = 0;
			*(void **)p = G_SavePtrOf (name, field->type == F_MMOVE);
		}
		break;

	default:
		gi.error ("ReadEdict: unknown field type");
	}
}

//=========================================================

/*
==============
WriteClient

All pointer variables (except function pointers) must be handled specially.
==============
*/
void WriteClient (FILE *f, gclient_t *client)
{
	field_t		*field;
	gclient_t	temp;
	
	// all of the ints, floats, and vectors stay as they are
	temp = *client;

	// change the pointers to lengths or indexes
	for (field=clientfields ; field->name ; field++)
	{
		WriteField1 (f, field, (byte *)&temp);
	}

	// write the block
	fwrite (&temp, sizeof(temp), 1, f);

	// now write any allocated data following the edict
	for (field=clientfields ; field->name ; field++)
	{
		WriteField2 (f, field, (byte *)client);
	}
}

/*
==============
ReadClient

All pointer variables (except function pointers) must be handled specially.
==============
*/
void ReadClient (FILE *f, gclient_t *client)
{
	field_t		*field;

	fread (client, sizeof(*client), 1, f);

	for (field=clientfields ; field->name ; field++)
	{
		ReadField (f, field, (byte *)client);
	}
}

/*
============
WriteGame

This will be called whenever the game goes to a new level,
and when the user explicitly saves the game.

Game information include cross level data, like multi level
triggers, help computer info, and all client states.

A single player death will automatically restore from the
last save position.
============
*/
/* BDGN 3.59z: the sizes of the structures written whole; a build with other sizes cannot read the save (clear error
   instead of garbage). write 1 = write them, 0 = read and compare (returns 0 on a difference). */
static int G_SaveSizes (FILE *f, int write)
{
	int sizes[4], old[4], i;
	sizes[0] = sizeof(game_locals_t); sizes[1] = sizeof(gclient_t); sizes[2] = sizeof(edict_t); sizes[3] = sizeof(level_locals_t);
	if (write) { fwrite (sizes, sizeof(sizes), 1, f); return 1; }
	if (fread (old, sizeof(old), 1, f) != 1) return 0;
	for (i = 0; i < 4; i++) if (old[i] != sizes[i]) return 0;
	return 1;
}
void WriteGame (char *filename, qboolean autosave)
{
	FILE	*f;
	int		i;
	char	str[16];

	if (!autosave)
		SaveClientData ();

	f = fopen (filename, "wb");
	if (!f)
		gi.error ("Couldn't open %s", filename);

	G_SaveNamesInit ();
	memset (str, 0, sizeof(str));
	strcpy (str, G_SAVE_MAGIC);	/* BDGN 3.59z: was __DATE__ (every rebuild on another day made old saves unloadable) */
	fwrite (str, sizeof(str), 1, f);
	G_SaveSizes (f, 1);

	game.autosaved = autosave;
	fwrite (&game, sizeof(game), 1, f);
	game.autosaved = false;

	for (i=0 ; i<game.maxclients ; i++)
		WriteClient (f, &game.clients[i]);

	fclose (f);
}

void ReadGame (char *filename)
{
	FILE	*f;
	int		i;
	char	str[16];

	gi.FreeTags (TAG_GAME);

	f = fopen (filename, "rb");
	if (!f)
		gi.error ("Couldn't open %s", filename);

	G_SaveNamesInit ();
	fread (str, sizeof(str), 1, f);
	str[sizeof(str) - 1] = 0;
	if (strcmp (str, G_SAVE_MAGIC))
	{
		fclose (f);
		gi.error ("Savegame from an older version (made before BDGN 3.59z, or by another game).\n");
	}
	if (!G_SaveSizes (f, 0))
	{
		fclose (f);
		gi.error ("Savegame from a build with other data structures (game, client, edict or level size differs).\n");
	}

	g_edicts =  gi.TagMalloc (game.maxentities * sizeof(g_edicts[0]), TAG_GAME);
	globals.edicts = g_edicts;

	fread (&game, sizeof(game), 1, f);
	game.clients = gi.TagMalloc (game.maxclients * sizeof(game.clients[0]), TAG_GAME);
	for (i=0 ; i<game.maxclients ; i++)
		ReadClient (f, &game.clients[i]);

	fclose (f);
}

//==========================================================


/*
==============
WriteEdict

All pointer variables (except function pointers) must be handled specially.
==============
*/
void WriteEdict (FILE *f, edict_t *ent)
{
	field_t		*field;
	edict_t		temp;

	// all of the ints, floats, and vectors stay as they are
	temp = *ent;

	// change the pointers to lengths or indexes
	for (field=fields ; field->name ; field++)
	{
		WriteField1 (f, field, (byte *)&temp);
	}

	// write the block
	fwrite (&temp, sizeof(temp), 1, f);

	// now write any allocated data following the edict
	for (field=fields ; field->name ; field++)
	{
		WriteField2 (f, field, (byte *)ent);
	}

}

/*
==============
WriteLevelLocals

All pointer variables (except function pointers) must be handled specially.
==============
*/
void WriteLevelLocals (FILE *f)
{
	field_t		*field;
	level_locals_t		temp;

	// all of the ints, floats, and vectors stay as they are
	temp = level;

	// change the pointers to lengths or indexes
	for (field=levelfields ; field->name ; field++)
	{
		WriteField1 (f, field, (byte *)&temp);
	}

	// write the block
	fwrite (&temp, sizeof(temp), 1, f);

	// now write any allocated data following the edict
	for (field=levelfields ; field->name ; field++)
	{
		WriteField2 (f, field, (byte *)&level);
	}
}


/*
==============
ReadEdict

All pointer variables (except function pointers) must be handled specially.
==============
*/
void ReadEdict (FILE *f, edict_t *ent)
{
	field_t		*field;

	fread (ent, sizeof(*ent), 1, f);

	for (field=fields ; field->name ; field++)
	{
		ReadField (f, field, (byte *)ent);
	}
}

/*
==============
ReadLevelLocals

All pointer variables (except function pointers) must be handled specially.
==============
*/
void ReadLevelLocals (FILE *f)
{
	field_t		*field;

	fread (&level, sizeof(level), 1, f);

	for (field=levelfields ; field->name ; field++)
	{
		ReadField (f, field, (byte *)&level);
	}
}

/*
=================
WriteLevel

=================
*/
void WriteLevel (char *filename)
{
	int		i;
	edict_t	*ent;
	FILE	*f;
	void	*base;

	f = fopen (filename, "wb");
	if (!f)
		gi.error ("Couldn't open %s", filename);

	// write out edict size for checking
	i = sizeof(edict_t);
	fwrite (&i, sizeof(i), 1, f);

	// write out a function pointer for checking
	/* BDGN 3.59z: no address check any more (names are saved); the magic and the sizes instead */
	G_SaveNamesInit ();
	base = (void *)0;
	fwrite (&base, sizeof(base), 1, f);
	G_SaveSizes (f, 1);

	// write out level_locals_t
	WriteLevelLocals (f);

	// write out all the entities
	for (i=0 ; i<globals.num_edicts ; i++)
	{
		ent = &g_edicts[i];
		if (!ent->inuse)
			continue;
		fwrite (&i, sizeof(i), 1, f);
		WriteEdict (f, ent);
	}
	i = -1;
	fwrite (&i, sizeof(i), 1, f);

	fclose (f);
}


/*
=================
ReadLevel

SpawnEntities will allready have been called on the
level the same way it was when the level was saved.

That is necessary to get the baselines
set up identically.

The server will have cleared all of the world links before
calling ReadLevel.

No clients are connected yet.
=================
*/
void ReadLevel (char *filename)
{
	int		entnum;
	FILE	*f;
	int		i;
	void	*base;
	edict_t	*ent;

	f = fopen (filename, "rb");
	if (!f)
		gi.error ("Couldn't open %s", filename);

	// free any dynamic memory allocated by loading the level
	// base state
	gi.FreeTags (TAG_LEVEL);

	// wipe all the entities
	memset (g_edicts, 0, game.maxentities*sizeof(g_edicts[0]));
	globals.num_edicts = maxclients->value+1;

	// check edict size
	fread (&i, sizeof(i), 1, f);
	if (i != sizeof(edict_t))
	{
		fclose (f);
		gi.error ("ReadLevel: mismatched edict size");
	}

	// check function pointer base address
	/* BDGN 3.59z: names are saved, so the address may differ; the structure sizes must not */
	G_SaveNamesInit ();
	fread (&base, sizeof(base), 1, f);
	if (base != (void *)0 || !G_SaveSizes (f, 0))
	{
		fclose (f);
		gi.error ("ReadLevel: level saved by an older or different build");
	}

	// load the level locals
	ReadLevelLocals (f);

	// load all the entities
	while (1)
	{
		if (fread (&entnum, sizeof(entnum), 1, f) != 1)
		{
			fclose (f);
			gi.error ("ReadLevel: failed to read entnum");
		}
		if (entnum == -1)
			break;
		if (entnum >= globals.num_edicts)
			globals.num_edicts = entnum+1;

		ent = &g_edicts[entnum];
		ReadEdict (f, ent);

		// let the server rebuild world links for this ent
		memset (&ent->area, 0, sizeof(ent->area));
		gi.linkentity (ent);
	}

	fclose (f);

	// mark all clients as unconnected
	for (i=0 ; i<maxclients->value ; i++)
	{
		ent = &g_edicts[i+1];
		ent->client = game.clients + i;
		ent->client->pers.connected = false;
	}

	// do any load time things at this point
	for (i=0 ; i<globals.num_edicts ; i++)
	{
		ent = &g_edicts[i];

		if (!ent->inuse)
			continue;

		// fire any cross-level triggers
		if (ent->classname)
			if (strcmp(ent->classname, "target_crosslevel_target") == 0)
				ent->nextthink = level.time + ent->delay;
	}
}
