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
// cvar.c -- dynamic variable tracking

#include "qcommon.h"

cvar_t	*cvar_vars;

/*
============
Cvar_InfoValidate
============
*/
static qboolean Cvar_InfoValidate (char *s)
{
	if (strstr (s, "\\"))
		return false;
	if (strstr (s, "\""))
		return false;
	if (strstr (s, ";"))
		return false;
	return true;
}

/*
============
Cvar_FindVar
============
*/
/* BDGN 3.55y: name lookup through a hash table. BDGN has several hundred more cvars than id's 3.21, and a
   lookup by name went through the whole list with strcmp; on a Pentium 133 every Cvar_VariableValue in a frame
   cost a full list walk. Same names, same case rules (exact strcmp) as before; the list itself is unchanged. */
static cvar_t	**cvar_hash;
static int		cvar_hash_size, cvar_hash_count;

static unsigned Cvar_HashName (const char *name)
{
	unsigned h = 2166136261u;
	while (*name)
		h = (h ^ (unsigned char)*name++) * 16777619u;
	return h;
}

static void Cvar_HashInsert (cvar_t *var)
{
	unsigned i;
	if (!cvar_hash || (cvar_hash_count + 1) * 10 > cvar_hash_size * 7)
	{	/* (re)build at most 70 % full */
		int size = cvar_hash_size ? cvar_hash_size * 2 : 2048;
		cvar_t **table = (cvar_t **)malloc (size * sizeof(cvar_t *)), *v;
		if (!table)
			return;	/* lookups fall back to the list */
		memset (table, 0, size * sizeof(cvar_t *));
		if (cvar_hash) free (cvar_hash);
		cvar_hash = table;
		cvar_hash_size = size;
		cvar_hash_count = 0;
		for (v = cvar_vars; v; v = v->next)
		{
			if (v == var) continue;
			for (i = Cvar_HashName (v->name) & (size - 1); cvar_hash[i]; i = (i + 1) & (size - 1))
				;
			cvar_hash[i] = v;
			cvar_hash_count++;
		}
	}
	for (i = Cvar_HashName (var->name) & (cvar_hash_size - 1); cvar_hash[i]; i = (i + 1) & (cvar_hash_size - 1))
		if (cvar_hash[i] == var) return;
	cvar_hash[i] = var;
	cvar_hash_count++;
}

static cvar_t *Cvar_FindVar (char *var_name)
{
	cvar_t	*var;
	unsigned i;

	if (cvar_hash)
	{
		for (i = Cvar_HashName (var_name) & (cvar_hash_size - 1); (var = cvar_hash[i]) != NULL; i = (i + 1) & (cvar_hash_size - 1))
			if (!strcmp (var_name, var->name))
				return var;
		return NULL;
	}
	for (var=cvar_vars ; var ; var=var->next)
		if (!strcmp (var_name, var->name))
			return var;

	return NULL;
}

/*
============
Cvar_VariableValue
============
*/
float Cvar_VariableValue (char *var_name)
{
	cvar_t	*var;
	
	var = Cvar_FindVar (var_name);
	if (!var)
		return 0;
	return var->value;	/* BDGN 3.55y: kept in step by Cvar_Get/Cvar_Set2/Cvar_FullSet */
}


/*
============
Cvar_VariableString
============
*/
char *Cvar_VariableString (char *var_name)
{
	cvar_t *var;
	
	var = Cvar_FindVar (var_name);
	if (!var)
		return "";
	return var->string;
}


/*
============
Cvar_CompleteVariable
============
*/
char *Cvar_CompleteVariable (char *partial)
{
	cvar_t		*cvar;
	int			len;
	
	len = strlen(partial);
	
	if (!len)
		return NULL;
		
	// check exact match
	for (cvar=cvar_vars ; cvar ; cvar=cvar->next)
		if (!strcmp (partial,cvar->name))
			return cvar->name;

	// check partial match
	for (cvar=cvar_vars ; cvar ; cvar=cvar->next)
		if (!strncmp (partial,cvar->name, len))
			return cvar->name;

	return NULL;
}


/*
============
Cvar_Get

If the variable already exists, the value will not be set
The flags will be or'ed in if the variable exists.
============
*/
cvar_t *Cvar_Get (char *var_name, char *var_value, int flags)
{
	cvar_t	*var;
	
	if (flags & (CVAR_USERINFO | CVAR_SERVERINFO))
	{
		if (!Cvar_InfoValidate (var_name))
		{
			Com_Printf("invalid info cvar name\n");
			return NULL;
		}
	}

	var = Cvar_FindVar (var_name);
	if (var)
	{
		var->flags |= flags;
		return var;
	}

	if (!var_value)
		return NULL;

	if (flags & (CVAR_USERINFO | CVAR_SERVERINFO))
	{
		if (!Cvar_InfoValidate (var_value))
		{
			Com_Printf("invalid info cvar value\n");
			return NULL;
		}
	}

	var = Z_Malloc (sizeof(*var));
	var->name = CopyString (var_name);
	var->string = CopyString (var_value);
	var->modified = true;
	var->value = atof (var->string);

	// link the variable in
	var->next = cvar_vars;
	cvar_vars = var;
	Cvar_HashInsert (var);	/* BDGN 3.55y */

	var->flags = flags;

	return var;
}

/*
============
Cvar_Set2
============
*/
/* BDGN 3.58k: config.cfg holds only saved (archived) cvars. One that is not registered yet when config.cfg runs
   (the other renderer's settings, settings registered later or only on first use) is created here; before 3.58k it
   was created without CVAR_ARCHIVE, so it was left out of config.cfg at the next quit and the setting was forgotten.
   Set while config.cfg runs (Cmd_Exec_f puts "cvar_cfgload 1" before and "cvar_cfgload 0" after it). */
static qboolean cvar_cfgload;

/* BDGN 3.58r (owner's rule: the game remembers every setting): a copy of every saved (archived) setting, taken when a
   benchmark starts and put back when it ends. The benchmarks restore their own list, but reading and applying presets
   (Ultra vs Faster, CLAUDE, Pa1983, K6-3 Special) also changed settings outside that list, which then stayed. */
static char		**cvar_snap;	/* name, value, name, value ... */
static int		cvar_snap_n, cvar_snap_time;

void Cvar_SnapshotFree (void)
{
	int i;
	for (i = 0; i < cvar_snap_n * 2; i++)
		Z_Free (cvar_snap[i]);
	if (cvar_snap)
		Z_Free (cvar_snap);
	cvar_snap = NULL;
	cvar_snap_n = 0;
}

/* take a copy unless one younger than maxage ms exists (a benchmark command that starts another one) */
void Cvar_SnapshotTake (int maxage)
{
	cvar_t	*var;
	int		n = 0, now = Sys_Milliseconds ();
	if (cvar_snap && now - cvar_snap_time < maxage)
		return;
	Cvar_SnapshotFree ();
	for (var = cvar_vars ; var ; var = var->next)
		if (var->flags & CVAR_ARCHIVE)
			n++;
	if (!n)
		return;
	cvar_snap = Z_Malloc (n * 2 * sizeof(char *));
	for (var = cvar_vars ; var && cvar_snap_n < n ; var = var->next)
		if (var->flags & CVAR_ARCHIVE)
		{
			cvar_snap[cvar_snap_n * 2] = CopyString (var->name);
			cvar_snap[cvar_snap_n * 2 + 1] = CopyString (var->latched_string ? var->latched_string : var->string);
			cvar_snap_n++;
		}
	cvar_snap_time = now;
}

/* put every copied value back (not the names in skip, which the caller restores itself); frees the copy */
int Cvar_SnapshotRestore (const char **skip)
{
	int i, k, changed = 0;
	for (i = 0; i < cvar_snap_n; i++)
	{
		const char *name = cvar_snap[i * 2], *value = cvar_snap[i * 2 + 1];
		cvar_t *var;
		for (k = 0; skip && skip[k] && Q_stricmp ((char *)skip[k], (char *)name); k++)
			;
		if (skip && skip[k])
			continue;
		var = Cvar_FindVar ((char *)name);
		if (!var || !strcmp (var->latched_string ? var->latched_string : var->string, value))
			continue;
		Cvar_Set ((char *)name, (char *)value);
		changed++;
	}
	Cvar_SnapshotFree ();
	return changed;
}

/* BDGN 3.58r: whether a cvar exists (client/cl_main.c CL_ArchiveUserSettings) */
qboolean Cvar_Exists (char *var_name)
{
	return Cvar_FindVar (var_name) != NULL;
}

static void Cvar_CfgLoad_f (void)
{
	cvar_cfgload = Cmd_Argc() > 1 && atoi (Cmd_Argv(1)) != 0;
}

cvar_t *Cvar_Set2 (char *var_name, char *value, qboolean force)
{
	cvar_t	*var;

	var = Cvar_FindVar (var_name);
	if (!var)
	{	// create it
		return Cvar_Get (var_name, value, cvar_cfgload ? CVAR_ARCHIVE : 0);
	}


	if (var->flags & (CVAR_USERINFO | CVAR_SERVERINFO))
	{
		if (!Cvar_InfoValidate (value))
		{
			Com_Printf("invalid info cvar value\n");
			return var;
		}
	}

	if (!force)
	{
		if (var->flags & CVAR_NOSET)
		{
			Com_Printf ("%s is write protected.\n", var_name);
			return var;
		}

		if (var->flags & CVAR_LATCH)
		{
			if (var->latched_string)
			{
				if (strcmp(value, var->latched_string) == 0)
					return var;
				Z_Free (var->latched_string);
			}
			else
			{
				if (strcmp(value, var->string) == 0)
					return var;
			}

			if (Com_ServerState())
			{
				Com_Printf ("%s will be changed for next game.\n", var_name);
				var->latched_string = CopyString(value);
			}
			else
			{
				var->string = CopyString(value);
				var->value = atof (var->string);
				if (!strcmp(var->name, "game"))
				{
					FS_SetGamedir (var->string);
					FS_ExecAutoexec ();
				}
			}
			return var;
		}
	}
	else
	{
		if (var->latched_string)
		{
			Z_Free (var->latched_string);
			var->latched_string = NULL;
		}
	}

	if (!strcmp(value, var->string))
		return var;		// not changed

	var->modified = true;

	if (var->flags & CVAR_USERINFO)
		userinfo_modified = true;	// transmit at next oportunity
	
	Z_Free (var->string);	// free the old value string
	
	var->string = CopyString(value);
	var->value = atof (var->string);

	return var;
}

/*
============
Cvar_ForceSet
============
*/
cvar_t *Cvar_ForceSet (char *var_name, char *value)
{
	return Cvar_Set2 (var_name, value, true);
}

/*
============
Cvar_Set
============
*/
cvar_t *Cvar_Set (char *var_name, char *value)
{
	return Cvar_Set2 (var_name, value, false);
}

/*
============
Cvar_FullSet
============
*/
cvar_t *Cvar_FullSet (char *var_name, char *value, int flags)
{
	cvar_t	*var;
	
	var = Cvar_FindVar (var_name);
	if (!var)
	{	// create it
		return Cvar_Get (var_name, value, flags);
	}


	var->modified = true;

	if (var->flags & CVAR_USERINFO)
		userinfo_modified = true;	// transmit at next oportunity
	
	Z_Free (var->string);	// free the old value string
	
	var->string = CopyString(value);
	var->value = atof (var->string);
	var->flags = flags;

	return var;
}

/*
============
Cvar_SetValue
============
*/
void Cvar_SetValue (char *var_name, float value)
{
	char	val[32];

	if (value == (int)value)
		Com_sprintf (val, sizeof(val), "%i",(int)value);
	else
		Com_sprintf (val, sizeof(val), "%f",value);
	Cvar_Set (var_name, val);
}


/*
============
Cvar_GetLatchedVars

Any variables with latched values will now be updated
============
*/
void Cvar_GetLatchedVars (void)
{
	cvar_t	*var;

	for (var = cvar_vars ; var ; var = var->next)
	{
		if (!var->latched_string)
			continue;
		Z_Free (var->string);
		var->string = var->latched_string;
		var->latched_string = NULL;
		var->value = atof(var->string);
		if (!strcmp(var->name, "game"))
		{
			FS_SetGamedir (var->string);
			FS_ExecAutoexec ();
		}
	}
}

/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
qboolean Cvar_Command (void)
{
	cvar_t			*v;

// check variables
	v = Cvar_FindVar (Cmd_Argv(0));
	if (!v)
		return false;
		
// perform a variable print or set
	if (Cmd_Argc() == 1)
	{
		Com_Printf ("\"%s\" is \"%s\"\n", v->name, v->string);
		return true;
	}

	Cvar_Set (v->name, Cmd_Argv(1));
	return true;
}


/*
============
Cvar_Set_f

Allows setting and defining of arbitrary cvars from console
============
*/
void Cvar_Set_f (void)
{
	int		c;
	int		flags;

	c = Cmd_Argc();
	if (c != 3 && c != 4)
	{
		Com_Printf ("usage: set <variable> <value> [u / s]\n");
		return;
	}

	if (c == 4)
	{
		if (!strcmp(Cmd_Argv(3), "u"))
			flags = CVAR_USERINFO;
		else if (!strcmp(Cmd_Argv(3), "s"))
			flags = CVAR_SERVERINFO;
		else
		{
			Com_Printf ("flags can only be 'u' or 's'\n");
			return;
		}
		Cvar_FullSet (Cmd_Argv(1), Cmd_Argv(2), flags);
	}
	else
		Cvar_Set (Cmd_Argv(1), Cmd_Argv(2));
}


/*
============
Cvar_WriteVariables

Appends lines containing "set variable value" for all variables
with the archive flag set to true.
============
*/
void Cvar_WriteVariables (char *path)
{
	cvar_t	*var;
	char	buffer[1024];
	FILE	*f;

	f = fopen (path, "a");
	for (var = cvar_vars ; var ; var = var->next)
	{
		if (var->flags & CVAR_ARCHIVE)
		{	/* BDGN 3.58k: a change that waits for a restart (latched) is the one to keep */
			Com_sprintf (buffer, sizeof(buffer), "set %s \"%s\"\n", var->name,
				var->latched_string ? var->latched_string : var->string);
			fprintf (f, "%s", buffer);
		}
	}
	fclose (f);
}

/*
============
Cvar_List_f

============
*/
void Cvar_List_f (void)
{
	cvar_t	*var;
	int		i;

	i = 0;
	for (var = cvar_vars ; var ; var = var->next, i++)
	{
		if (var->flags & CVAR_ARCHIVE)
			Com_Printf ("*");
		else
			Com_Printf (" ");
		if (var->flags & CVAR_USERINFO)
			Com_Printf ("U");
		else
			Com_Printf (" ");
		if (var->flags & CVAR_SERVERINFO)
			Com_Printf ("S");
		else
			Com_Printf (" ");
		if (var->flags & CVAR_NOSET)
			Com_Printf ("-");
		else if (var->flags & CVAR_LATCH)
			Com_Printf ("L");
		else
			Com_Printf (" ");
		Com_Printf (" %s \"%s\"\n", var->name, var->string);
	}
	Com_Printf ("%i cvars\n", i);
}


qboolean userinfo_modified;


char	*Cvar_BitInfo (int bit)
{
	static char	info[MAX_INFO_STRING];
	cvar_t	*var;

	info[0] = 0;

	for (var = cvar_vars ; var ; var = var->next)
	{
		if (var->flags & bit)
			Info_SetValueForKey (info, var->name, var->string);
	}
	return info;
}

// returns an info string containing all the CVAR_USERINFO cvars
char	*Cvar_Userinfo (void)
{
	return Cvar_BitInfo (CVAR_USERINFO);
}

// returns an info string containing all the CVAR_SERVERINFO cvars
char	*Cvar_Serverinfo (void)
{
	return Cvar_BitInfo (CVAR_SERVERINFO);
}

/*
============
Cvar_Init

Reads in all archived cvars
============
*/
void Cvar_Init (void)
{
	Cmd_AddCommand ("set", Cvar_Set_f);
	Cmd_AddCommand ("cvarlist", Cvar_List_f);
	Cmd_AddCommand ("cvar_cfgload", Cvar_CfgLoad_f);	/* BDGN 3.58k */

}

/* BDGN 3.59z (Q2PRO): every cvar that starts with partial, for the console's list of matches */
int Cvar_CompleteList (const char *partial, void (*found) (const char *name))
{
	cvar_t *v;
	int n = 0, len = (int)strlen (partial);
	for (v = cvar_vars; v; v = v->next)
		if (!Q_strncasecmp ((char *)partial, v->name, len)) { found (v->name); n++; }
	return n;
}

/* BDGN 3.59z: lookup without creating (console value completion) */
cvar_t *Cvar_Find (char *var_name)
{
	return Cvar_FindVar (var_name);
}
