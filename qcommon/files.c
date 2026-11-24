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

#include "qcommon.h"

// define this to dissalow any data but the demo pak file
//#define	NO_ADDONS

// if a packfile directory differs from this, it is assumed to be hacked
// Full version
#define	PAK0_CHECKSUM	0x40e614e0
// Demo
//#define	PAK0_CHECKSUM	0xb2c6d7ea
// OEM
//#define	PAK0_CHECKSUM	0x78e135c

/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/


//
// in memory
//

typedef struct
{
	char	name[MAX_QPATH];
	int		filepos, filelen;
} packfile_t;

typedef struct pack_s
{
	char	filename[MAX_OSPATH];
	FILE	*handle;
	int		numfiles;
	packfile_t	*files;
	int		*index;		// BDGN 3.51ar: name index for a directory that is not strictly sorted, NULL if sorted (fs_packfind.h)
} pack_t;

#include "fs_packfind.h"	// BDGN 3.51p

char	fs_gamedir[MAX_OSPATH];
cvar_t	*fs_basedir;
cvar_t	*fs_cddir;
cvar_t	*fs_gamedirvar;

typedef struct filelink_s
{
	struct filelink_s	*next;
	char	*from;
	int		fromlength;
	char	*to;
} filelink_t;

filelink_t	*fs_links;

typedef struct searchpath_s
{
	char	filename[MAX_OSPATH];
	pack_t	*pack;		// only one of filename / pack will be used
	struct searchpath_s *next;
} searchpath_t;

searchpath_t	*fs_searchpaths;
searchpath_t	*fs_base_searchpaths;	// without gamedirs

// BDGN 3.32: optional low quality packs (not part of the normal pak0..pak9 scheme)
#define BDGN_MAX_FILES_IN_PACK	65536	/* BDGN 3.51af: FS_LoadPackFile */
#define LOWTEX_PAK	"pak6-lowtextures.pak"
#define LOWSND_PAK	"pak7-lowsounds.pak"
#define LOWSND_4_PAK "pak7-lowsounds-4khz.pak"
#define HDFX_PAK "pak8-hdfx.pak"	/* BDGN 3.55s: HD weapon effects (sprites/hdfx/), always searched; not on legacy machines */
#define HDFX3D_PAK "pak8-hdfx3d.pak"	/* BDGN 3.55t: modern 3D weapon effects (models/hdfx3d/); not on legacy machines */
#define HDGORE_PAK "pak8-hdgore.pak"	/* BDGN 3.55y: marks, 3D railgun, HD gibs and blood (models/hdgore/); not on legacy machines */
#define HDFX3DSD_PAK "pak8-hdfx3d-sd.pak"	/* BDGN 3.55v: AI (Stable Diffusion) textures for them (models/hdfx3d_sd/), optional */
/* BDGN 3.56r: the HD 4K choices of the effect rows. Each pack holds DDS (DXT1 / DXT5, mipmaps) under the same names as
   the TGA of its HD pack; the renderer takes the DDS when it is found (ref_gl/gl_image.c GL_LoadFX4K), so a pack is
   searched only while its row is on HD 4K:
     pak8-hdfx-4k.pak       sprites/hdfx/          cl_hdfx 2           (HD sprite effects: HD 4K)
     pak8-hdgore-4k.pak     models/hdgore/decals*, actor_marks*: cl_decals 2  (bullet holes / marks: HD 4K)
                            the rest (gibs, smoke, puffs):     cl_fx3d_tex 2 or 3
     pak8-hdfx3d-4k.pak     models/hdfx3d/         cl_fx3d_tex 2       (3D effect textures: standard 4K)
     pak8-hdfx3d-sd-4k.pak  models/hdfx3d_sd/      cl_fx3d_tex 3       (3D effect textures: AI 4K) */
#define HDFX_4K_PAK "pak8-hdfx-4k.pak"
#define HDGORE_4K_PAK "pak8-hdgore-4k.pak"
#define HDFX3D_4K_PAK "pak8-hdfx3d-4k.pak"
#define HDFX3DSD_4K_PAK "pak8-hdfx3d-sd-4k.pak"
#define HDSND_PAK "pak7-hd-sounds44.pak"	/* BDGN 3.55g: 44.1 kHz 16-bit sounds, searched only at s_khz 44; 3.55j: renamed from pak8-hd-sounds44.pak (sound packs pak7-, texture packs pak8-) */
#define LOWTEX_SMALL_PAK	"lowtex-small.pak"	/* BDGN 3.51bdt: TEST, half-size textures (fs_lowtextures 2, BDGN ref_gl only);
											   not named pakN.pak, so older builds never load it */
#define LOWTEX_ULTRA_PAK "lowtex-ultra.pak"
/* BDGN 3.55i: three interchangeable HD texture sets with the same file names (textures_hd/...); gl_hdtextures picks
   one: 1 = enhanced (pak8-hdx-*), 2 = standard (pak8-hd-*), 3 = enhanced, less grain (pak8-hdx2-*). All installed
   sets are in the search path; only the chosen one is searched. */
static const char *hd_pack_parts[]={"e1","e2","e3","models","players","sky"};
static const char *hd_set_prefix[6]={NULL,"pak8-hdx-","pak8-hd-","pak8-hdx2-","pak8-hdsd-","pak8-hd4k-"};	/* 3.55w: 5 = AI 4K (DDS) */	/* 3.55k: 4 = AI (Stable Diffusion) */
#define HD_SET_COUNT 6
static cvar_t *fs_hdtextures;
static int FS_HDSetOfPack (const char *filename)
{
 const char *base=strrchr(filename,'/');
 const char *base2=strrchr(filename,'\\');
 if (base2>base) base=base2;
 base=base?base+1:filename;
 if (!Q_strncasecmp((char *)base,"pak8-hd4k-",10)) return 5;	/* 3.55w */
 if (!Q_strncasecmp((char *)base,"pak8-hdsd-",10)) return 4;
 if (!Q_strncasecmp((char *)base,"pak8-hdx2-",10)) return 3;
 if (!Q_strncasecmp((char *)base,"pak8-hdx-",9)) return 1;
 if (!Q_strncasecmp((char *)base,"pak8-hd-",8) && Q_strncasecmp((char *)base,"pak8-hd-sounds",14)) return 2;	/* an old pak8-hd-sounds44.pak is not a texture set (and is no longer loaded) */
 return 0;
}
cvar_t	*fs_lowtextures;	// 1 = half-size, 2 = quarter-size with half-size fallback
static cvar_t *fs_sound_rate;

/*
================
FS_PakEnabled

The two low quality packs are always loaded into the search path, but only
searched while their cvar is set, so the setting can be changed at run time.
================
*/
static cvar_t *fs_vanilla;
static const char *FS_BaseName (const char *path);
static int FS_FX4KPack (const char *base)
{
	if (!Q_stricmp ((char *)base, HDFX_4K_PAK)) return 1;
	if (!Q_stricmp ((char *)base, HDGORE_4K_PAK)) return 2;
	if (!Q_stricmp ((char *)base, HDFX3D_4K_PAK)) return 3;
	if (!Q_stricmp ((char *)base, HDFX3DSD_4K_PAK)) return 4;
	return 0;
}
static qboolean FS_FX4KEnabled (int which, const char *filename)
{
	static cvar_t *hdfx, *decals, *fx3dtex;
	if (!hdfx)
	{
		hdfx = Cvar_Get ("cl_hdfx", "0", CVAR_ARCHIVE);
		decals = Cvar_Get ("cl_decals", "0", CVAR_ARCHIVE);
		fx3dtex = Cvar_Get ("cl_fx3d_tex", "0", CVAR_ARCHIVE);
	}
	switch (which)
	{
	case 1: return hdfx->value == 2;
	case 2:
		if (filename && (!Q_strncasecmp ((char *)filename, "models/hdgore/decals", 20) ||
		                 !Q_strncasecmp ((char *)filename, "models/hdgore/actor_marks", 25)))
			return decals->value == 2;
		return fx3dtex->value >= 2;
	case 3: return fx3dtex->value == 2;
	case 4: return fx3dtex->value == 3;
	}
	return false;
}
static qboolean FS_PakEnabled (pack_t *pak, const char *filename)
{
	qboolean ultra = strstr (pak->filename, LOWTEX_ULTRA_PAK) != NULL;
	if (!fs_vanilla)
		fs_vanilla = Cvar_Get ("fs_vanilla", "0", CVAR_ARCHIVE);
	if (fs_vanilla->value)
	{	/* BDGN 3.55al, the "default config" rule: Quake II as in 1998. Only id's own paks are searched - pak0.pak
		   (the game), pak1.pak and pak2.pak (id's 3.14 and 3.20 point releases) - whatever else is in the folder.
		   The one exception is the preset files (configs/, *.cfg), or no other preset could be chosen again. */
		const char *base = pak->filename + strlen (pak->filename);
		size_t n = filename ? strlen (filename) : 0;
		while (base > pak->filename && base[-1] != '/' && base[-1] != '\\' && base[-1] != ':')
			base--;
		if (Q_stricmp ((char *)base, "pak0.pak") && Q_stricmp ((char *)base, "pak1.pak") && Q_stricmp ((char *)base, "pak2.pak"))
		{
			if (!filename || (Q_strncasecmp ((char *)filename, "configs/", 8) && (n < 4 || Q_stricmp ((char *)filename + n - 4, ".cfg")) &&
			    Q_stricmp ((char *)filename, "pics/bdgn_conback.pcx")))
				return false;	/* BDGN 3.57f: also the Brain Drain console picture of pak4 (the old loose copy had 50
				                   transparent pixels, colour 255, which flickered as dots in front of the game) */
		}
	}
	if (ultra || strstr (pak->filename, LOWTEX_SMALL_PAK))
	{	/* BDGN 3.51bdt: only with BDGN's OpenGL renderer (it keeps the halved WALs aligned); 3.52c: the only low
		   fidelity texture pack, fs_lowtextures 1 (or the old 2).  3.52y: also BDGN's software renderer, wall
		   textures (.wal) only: it draws the halved WALs at the original size from their own mip levels; skins,
		   sprites and skies of the pack would need the exact sizes there, so those come from the normal paks. */
		static cvar_t *vref;
		size_t n;
		if (!vref)
			vref = Cvar_Get ("vid_ref", "soft", CVAR_ARCHIVE);
		if (!fs_lowtextures || !fs_lowtextures->value)
			return false;
		if (ultra && (fs_lowtextures->value != 2 ||
		    (strcmp(vref->string,"soft") && strcmp(vref->string,"gl"))))
			return false;
		if (!strcmp (vref->string, "soft"))
		{	/* BDGN 3.56r: the software renderer takes the walls and, since 3.56r, the skins of models and players
			   (ref_soft/r_alias.c scales the skin coordinates to the smaller picture). Sprites and skies of the packs
			   would need the original sizes there and come from the normal paks. */
			n = filename ? strlen (filename) : 0;
			if (n < 4)
				return false;
			if (Q_stricmp ((char *)filename + n - 4, ".wal") &&
			    !((!Q_strncasecmp ((char *)filename, "models/", 7) || !Q_strncasecmp ((char *)filename, "players/", 8)) &&
			      !Q_stricmp ((char *)filename + n - 4, ".pcx") && Cvar_VariableValue ("sw_lowskins_capable")))
				return false;
		}
		/* BDGN 3.56r: OpenGL takes everything of both packs (walls, skins, sprites, skies): before, the quarter-size
		   pack was limited to walls, so "extra low" left weapons, enemies and the sky at half or full size. */
		else if (strcmp (vref->string, "gl") &&
			!(!strcmp (vref->string, "glam") && Cvar_VariableValue ("glam_bdgn_features") >= 3522))
			return false;	/* 3.52z: AMD's ref_glam patched to 3.52z keeps the halved WALs aligned as ref_gl does */
	}
	if (strstr(pak->filename,LOWSND_4_PAK))
		return fs_sound_rate->value==4;
	if (strstr(pak->filename,LOWSND_PAK))
		return fs_sound_rate->value==8;
	if (strstr(pak->filename,HDSND_PAK))
		return fs_sound_rate->value==44;
	{
		int fx4k=FS_FX4KPack(FS_BaseName(pak->filename));
		if (fx4k)
			return FS_FX4KEnabled(fx4k,filename);
	}
	{
		int set=FS_HDSetOfPack(pak->filename);
		if (set) {
			if (!fs_hdtextures) fs_hdtextures=Cvar_Get("gl_hdtextures","0",CVAR_ARCHIVE);
			return (int)fs_hdtextures->value==set;
		}
	}
	return true;
}

/* BDGN 3.52g: the preset cfg files live in configs/ (inside pak5.pak).  Returns "configs/<name>" when that exists,
   else <name> (an older pak5 or a loose file in baseq2). */
char *FS_PresetPath (const char *name)
{
	static char	path[2][MAX_QPATH];
	static int	n;
	char		*p = path[n ^= 1];
	Com_sprintf (p, MAX_QPATH, "configs/%s", name);
	if (FS_LoadFile (p, NULL) > 0)
		return p;
	Com_sprintf (p, MAX_QPATH, "%s", name);
	return p;
}

static qboolean FS_NamedPackAvailable (const char *name)
{
 searchpath_t *s;
 for(s=fs_searchpaths;s;s=s->next)
  if(s->pack && strstr(s->pack->filename,name)) return true;
 return false;
}

qboolean FS_SmallTexPackAvailable (void)
{
 return FS_NamedPackAvailable(LOWTEX_SMALL_PAK);
}

qboolean FS_UltraTexPackAvailable (void)
{
 return FS_NamedPackAvailable(LOWTEX_ULTRA_PAK);
}

/* BDGN 3.56r: an effect pack by file name is installed (loaded into the search path, even while not searched) */
qboolean FS_EffectPackInstalled (const char *name)
{
 return FS_NamedPackAvailable(name);
}

qboolean FS_HDSoundPackAvailable (void)
{
 return FS_NamedPackAvailable(HDSND_PAK);
}

/* BDGN 3.55i: pak7-lowsounds-4khz.pak installed (presets choose 4 kHz only with it) */
qboolean FS_LowSound4PackAvailable (void)
{
 return FS_NamedPackAvailable(LOWSND_4_PAK);
}

/* BDGN 3.55i: set 1..4 (see hd_set_prefix) has at least one of its PAKs loaded; set 0 = any set */
qboolean FS_HDTexSetAvailable (int set)
{
 searchpath_t *s;
 for(s=fs_searchpaths;s;s=s->next)
  if(s->pack && FS_HDSetOfPack(s->pack->filename) && (!set || FS_HDSetOfPack(s->pack->filename)==set)) return true;
 return false;
}
qboolean FS_HDTexPackAvailable (void)
{
 return FS_HDTexSetAvailable(0);
}
qboolean FS_LowPackAvailable (qboolean textures)
{
 searchpath_t *s;
 const char *name=textures ? LOWTEX_SMALL_PAK : LOWSND_PAK;	/* 3.52c: lowtex-small.pak replaces pak6-lowtextures.pak */
 for(s=fs_searchpaths;s;s=s->next)
  if(s->pack && strstr(s->pack->filename,name)) return true;
 return false;
}



/*

All of Quake's data access is through a hierchal file system, but the contents of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and all game directories.  The sys_* files pass this to host_init in quakeparms_t->basedir.  This can be overridden with the "-basedir" command line parm to allow code debugging in a different directory.  The base directory is
only used during filesystem initialization.

The "game directory" is the first tree on the search path and directory that all generated files (savegames, screenshots, demos, config files) will be saved to.  This can be overridden with the "-game" command line parameter.  The game directory can never be changed while quake is executing.  This is a precacution against having a malicious server instruct clients to write files over areas they shouldn't.

*/


/*
================
FS_filelength
================
*/
int FS_filelength (FILE *f)
{
	int		pos;
	int		end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}


/*
============
FS_CreatePath

Creates any directories needed to store the given filename
============
*/
void	FS_CreatePath (char *path)
{
	char	*ofs;
	
	for (ofs = path+1 ; *ofs ; ofs++)
	{
		if (*ofs == '/')
		{	// create the directory
			*ofs = 0;
			Sys_Mkdir (path);
			*ofs = '/';
		}
	}
}


/*
==============
FS_FCloseFile

For some reason, other dll's can't just cal fclose()
on files returned by FS_FOpenFile...
==============
*/
void FS_FCloseFile (FILE *f)
{
	fclose (f);
}


// RAFAEL
/*
	Developer_searchpath
*/
int	Developer_searchpath (int who)
{
	
	int		ch;
	// PMM - warning removal
//	char	*start;
	searchpath_t	*search;
	
	if (who == 1) // xatrix
		ch = 'x';
	else if (who == 2)
		ch = 'r';

	for (search = fs_searchpaths ; search ; search = search->next)
	{
		if (strstr (search->filename, "xatrix"))
			return 1;

		if (strstr (search->filename, "rogue"))
			return 2;
/*
		start = strchr (search->filename, ch);

		if (start == NULL)
			continue;

		if (strcmp (start ,"xatrix") == 0)
			return (1);
*/
	}
	return (0);

}


/*
===========
FS_FOpenFile

Finds the file in the search path.
returns filesize and an open FILE *
Used for streaming data out of either a pak file or
a seperate file.
===========
*/
int file_from_pak = 0;
#ifndef NO_ADDONS


/*
================
BDGN 3.59f: map load statistics and the miss cache (fs_misscache)

A map load asks for many names that are in no pak and no folder: HD replacements, optional BDGN sprites and models
that are not installed. Each miss searched every pak and tried fopen in every folder (Windows 95/98: a directory
lookup on the disk each time). While a load runs (FS_LoadBegin .. FS_LoadEnd) a miss is remembered, and the next
load asks the disk again only if something that decides which paks are searched has changed (the settings in
FS_MissSignature, the game folder, a download). Same files are found as before; fs_misscache 0 turns it off.
================
*/
static cvar_t	*fs_misscache;
int				fs_stat_opens, fs_stat_misses, fs_stat_cachehits;
unsigned		fs_stat_us;
#define FS_MISS_HASH	1024
#define FS_MISS_MAX		4096
typedef struct fsmiss_s { struct fsmiss_s *next; char name[4]; } fsmiss_t;
static fsmiss_t	*fs_miss[FS_MISS_HASH];
static int		fs_miss_count, fs_miss_active;
static char		fs_miss_sig[512];
extern unsigned	Sys_Microseconds (void);

static unsigned FS_MissHash (const char *s)
{
	unsigned h = 0;
	for (; *s; s++)
	{
		int c = *s;
		if (c >= 'A' && c <= 'Z') c += 32;
		if (c == '\\') c = '/';
		h = h * 31 + c;
	}
	return h & (FS_MISS_HASH - 1);
}
void FS_MissCacheClear (void)
{
	int i;
	for (i = 0; i < FS_MISS_HASH; i++)
		while (fs_miss[i])
		{
			fsmiss_t *n = fs_miss[i]->next;
			free (fs_miss[i]);
			fs_miss[i] = n;
		}
	fs_miss_count = 0;
	fs_miss_sig[0] = 0;
}
static void FS_StrCat (char *out, const char *in, int size)
{
	int l = (int)strlen (out);
	while (*in && l < size - 1)
		out[l++] = *in++;
	out[l] = 0;
}
static void FS_MissSignature (char *out, int size)
{
	static const char *names[] = { "fs_vanilla","fs_lowtextures","vid_ref","s_khz","gl_hdtextures","cl_hdfx","cl_decals",
		"cl_fx3d_tex","cl_simplemodels","cl_enemyskins","cl_retromode","sw_lowskins_capable","glam_bdgn_features",
		"bdgn_legacy_graphics","bdgn_extra_lowram","game" };
	int i;
	out[0] = 0;
	for (i = 0; i < (int)(sizeof(names)/sizeof(names[0])); i++)
	{
		FS_StrCat (out, Cvar_VariableString ((char *)names[i]), size);
		FS_StrCat (out, "|", size);
	}
}
void FS_LoadBegin (void)
{
	char sig[512];
	if (!fs_misscache)
		fs_misscache = Cvar_Get ("fs_misscache", "1", CVAR_ARCHIVE);
	fs_stat_opens = fs_stat_misses = fs_stat_cachehits = 0;
	fs_stat_us = 0;
	FS_MissSignature (sig, sizeof(sig));
	if (!fs_misscache->value || strcmp (sig, fs_miss_sig))
	{
		FS_MissCacheClear ();
		strcpy (fs_miss_sig, sig);
	}
	fs_miss_active = fs_misscache->value != 0;
}
void FS_LoadEnd (void)
{
	fs_miss_active = 0;
}
static int FS_MissFind (const char *name)
{
	fsmiss_t *m;
	for (m = fs_miss[FS_MissHash (name)]; m; m = m->next)
		if (!Q_stricmp (m->name, (char *)name))
			return 1;
	return 0;
}
static void FS_MissAdd (const char *name)
{
	int l = (int)strlen (name);
	unsigned h;
	fsmiss_t *m;
	if (fs_miss_count >= FS_MISS_MAX || l >= MAX_QPATH)
		return;
	m = malloc (sizeof(fsmiss_t) + l);
	if (!m)
		return;
	strcpy (m->name, name);
	h = FS_MissHash (name);
	m->next = fs_miss[h];
	fs_miss[h] = m;
	fs_miss_count++;
}
static int FS_FOpenFileReal (char *filename, FILE **file);
int FS_FOpenFile (char *filename, FILE **file)
{
	unsigned t0 = Sys_Microseconds ();
	int len;
	if (fs_miss_active && FS_MissFind (filename))
	{
		file_from_pak = 0;
		*file = NULL;
		fs_stat_cachehits++;
		fs_stat_us += Sys_Microseconds () - t0;
		return -1;
	}
	len = FS_FOpenFileReal (filename, file);
	if (*file)
		fs_stat_opens++;
	else
	{
		fs_stat_misses++;
		if (fs_miss_active)
			FS_MissAdd (filename);
	}
	fs_stat_us += Sys_Microseconds () - t0;
	return len;
}

static int FS_FOpenFileReal (char *filename, FILE **file)
{
	searchpath_t	*search;
	char			netpath[MAX_OSPATH];
	pack_t			*pak;
	int				i;
	filelink_t		*link;

	file_from_pak = 0;

	/* The saved client settings are local to the writable game directory. */
	if (!Q_stricmp (filename, "config.cfg"))
	{
		Com_sprintf (netpath, sizeof(netpath), "%s/config.cfg", FS_Gamedir());
		*file = fopen (netpath, "rb");
		return *file ? FS_filelength (*file) : -1;
	}

	// check for links first
	for (link = fs_links ; link ; link=link->next)
	{
		if (!strncmp (filename, link->from, link->fromlength))
		{
			Com_sprintf (netpath, sizeof(netpath), "%s%s",link->to, filename+link->fromlength);
			*file = fopen (netpath, "rb");
			if (*file)
			{		
				Com_DPrintf ("link file: %s\n",netpath);
				return FS_filelength (*file);
			}
			return -1;
		}
	}

//
// search through the path, one element at a time
//
	for (search = fs_searchpaths ; search ; search = search->next)
	{
	// is the element a pak file?
		if (search->pack)
		{
		// look through all the pak file elements
			pak = search->pack;
			if (!FS_PakEnabled (pak, filename))	// BDGN 3.32: low quality pack switched off
				continue;
			i = FS_PackFind (pak->files, pak->numfiles, pak->index, filename);	// BDGN 3.51ar: binary search in every pak, same result as id's linear search (fs_packfind.h)
			if(i>=0)
				{	// found it!
					file_from_pak = 1;
					Com_DPrintf ("PackFile: %s : %s\n",pak->filename, filename);
				// open a new file on the pakfile
					*file = fopen (pak->filename, "rb");
					if (!*file)
						Com_Error (ERR_FATAL, "Couldn't reopen %s", pak->filename);	
					fseek (*file, pak->files[i].filepos, SEEK_SET);
					return pak->files[i].filelen;
				}
		}
		else
		{		
	// check a file in the directory tree
			
			Com_sprintf (netpath, sizeof(netpath), "%s/%s",search->filename, filename);
			
			*file = fopen (netpath, "rb");
			if (!*file)
				continue;
			
			Com_DPrintf ("FindFile: %s\n",netpath);

			return FS_filelength (*file);
		}
		
	}
	
	Com_DPrintf ("FindFile: can't find %s\n", filename);
	
	*file = NULL;
	return -1;
}

/* BDGN 3.51q: which PAK file or directory supplies a name, as FS_FOpenFile would find it (NULL = nowhere).
   Diagnostics only (bdgn_spritecheck); the low quality packs are skipped while switched off, as in FS_FOpenFile. */
const char *FS_WhereIs (const char *filename)
{
	searchpath_t	*search;
	char			netpath[MAX_OSPATH];
	FILE			*f;

	for (search = fs_searchpaths ; search ; search = search->next)
	{
		if (search->pack)
		{
			if (!FS_PakEnabled (search->pack, filename))
				continue;
			if (FS_PackFind (search->pack->files, search->pack->numfiles, search->pack->index, filename) >= 0)
				return search->pack->filename;
		}
		else
		{
			Com_sprintf (netpath, sizeof(netpath), "%s/%s", search->filename, filename);
			f = fopen (netpath, "rb");
			if (f)
			{
				fclose (f);
				return search->filename;
			}
		}
	}
	return NULL;
}

#else

// this is just for demos to prevent add on hacking

int FS_FOpenFile (char *filename, FILE **file)
{
	searchpath_t	*search;
	char			netpath[MAX_OSPATH];
	pack_t			*pak;
	int				i;

	file_from_pak = 0;

	// get config from directory, everything else from pak
	if (!strcmp(filename, "config.cfg") || !strncmp(filename, "players/", 8))
	{
		Com_sprintf (netpath, sizeof(netpath), "%s/%s",FS_Gamedir(), filename);
		
		*file = fopen (netpath, "rb");
		if (!*file)
			return -1;
		
		Com_DPrintf ("FindFile: %s\n",netpath);

		return FS_filelength (*file);
	}

	for (search = fs_searchpaths ; search ; search = search->next)
		if (search->pack)
			break;
	if (!search)
	{
		*file = NULL;
		return -1;
	}

	pak = search->pack;
	for (i=0 ; i<pak->numfiles ; i++)
		if (!Q_strcasecmp (pak->files[i].name, filename))
		{	// found it!
			file_from_pak = 1;
			Com_DPrintf ("PackFile: %s : %s\n",pak->filename, filename);
		// open a new file on the pakfile
			*file = fopen (pak->filename, "rb");
			if (!*file)
				Com_Error (ERR_FATAL, "Couldn't reopen %s", pak->filename);	
			fseek (*file, pak->files[i].filepos, SEEK_SET);
			return pak->files[i].filelen;
		}
	
	Com_DPrintf ("FindFile: can't find %s\n", filename);
	
	*file = NULL;
	return -1;
}

#endif


/*
=================
FS_ReadFile

Properly handles partial reads
=================
*/
void CDAudio_Stop(void);
#define	MAX_READ	0x10000		// read in blocks of 64k
void FS_Read (void *buffer, int len, FILE *f)
{
	int		block, remaining;
	int		read;
	byte	*buf;
	int		tries;

	buf = (byte *)buffer;

	// read in chunks for progress bar
	remaining = len;
	tries = 0;
	while (remaining)
	{
		block = remaining;
		if (block > MAX_READ)
			block = MAX_READ;
		read = fread (buf, 1, block, f);
		if (read == 0)
		{
			// we might have been trying to read from a CD
			if (!tries)
			{
				tries = 1;
				CDAudio_Stop();
			}
			else
				Com_Error (ERR_FATAL, "FS_Read: 0 bytes read");
		}

		if (read == -1)
			Com_Error (ERR_FATAL, "FS_Read: -1 bytes read");

		// do some progress bar thing here...

		remaining -= read;
		buf += read;
	}
}

/*
============
FS_LoadFile

Filename are reletive to the quake search path
a null buffer will just return the file length without loading
============
*/
int FS_LoadFile (char *path, void **buffer)
{
	FILE	*h;
	byte	*buf;
	int		len;
	static cvar_t *cl_simplemodels;	/* BDGN 3.57l */

	buf = NULL;	// quiet compiler warning
	h = NULL;
	len = -1;

	/* BDGN 3.57l: simpler 3D models for the software renderer and slow computers (bdgn_simplemodels.pak).
	   cl_simplemodels 1/2/3 = simple/simpler/simplest: models/<x>.md2 is first looked for as models/simple<n>/<x>.md2
	   (fewer triangles, smaller skins; the skin paths inside point to models/simple<n>/ as well). The model keeps its
	   own name, so everything that compares model names works as before; a missing simple model gives the normal one.
	   Not in the vanilla preset (fs_vanilla). Models are kept until vid_restart, so the menu restarts the renderer. */
	if (!cl_simplemodels)
		cl_simplemodels = Cvar_Get ("cl_simplemodels", "0", CVAR_ARCHIVE);
	if (path && cl_simplemodels->value >= 1 && cl_simplemodels->value <= 3 && !Cvar_VariableValue ("fs_vanilla"))
	{
		size_t n = strlen (path);
		/* 3.58d-3.58n: never the weapon in the hand (models/weapons/v_*): the simpler one shook while moving (owner's
		   video, super shotgun). 3.58o: back, MEASURED on the K6-3+ (Software 320x240): without it simplest models gave
		   +1.7 % instead of +6.7 % on top of Faster and PSYCHO lost 8.8 % */
		if (n > 11 && n < MAX_QPATH - 9 && !Q_strncasecmp (path, "models/", 7) && Q_strncasecmp (path + 7, "simple", 6) &&
		    !Q_stricmp (path + n - 4, ".md2"))	/* 3.58o: the view weapons again, rebuilt without the shaking
		    (_extra/simplemodels/build_view_weapons.py: only id's own vertices, merged only when together in every frame) */
		{
			char simple[MAX_QPATH];
			Com_sprintf (simple, sizeof(simple), "models/simple%d/%s", (int)cl_simplemodels->value, path + 7);
			len = FS_FOpenFile (simple, &h);
		}
	}

	/* BDGN 3.58y (owner 2026-11-21): enemy skin sets (bdgn_enemyskins.pak), only in Wolfenstein 3D mode (not in
	   classic 2d or the normal game; the 2D soldiers there use banks drawn from these skins, qcommon/duke_playback.h).
	   cl_enemyskins 1 = the Second World War
	   German field uniform (feldgrau tunic, grey trousers, black boots and belts, field-grey helmet; no insignia):
	   models/[simple<n>/]monsters/<m>/<skin>.pcx is first looked for as .../monsters/<m>/ww2/<skin>.pcx. A model without
	   such a skin keeps its own. The renderer keeps skins until vid_restart, so the menu restarts it. */
	if (!h && path && !Cvar_VariableValue ("fs_vanilla"))
	{
		static cvar_t *cl_enemyskins;
		if (!cl_enemyskins)
			cl_enemyskins = Cvar_Get ("cl_enemyskins", "0", CVAR_ARCHIVE);
		if (cl_enemyskins->value == 1 && (Cvar_VariableValue ("cl_retromode") == 2 || Cvar_VariableValue ("cl_retromode") == 3))	/* only in Wolfenstein 3D mode (3.60i: 4 is Duke Nukem 3D) */
		{
			size_t n = strlen (path);
			const char *m = strstr (path, "monsters/");
			if (m && n > 4 && n < MAX_QPATH - 5 && !Q_strncasecmp (path, "models/", 7) && !Q_stricmp (path + n - 4, ".pcx"))
			{
				const char *file = strrchr (path, '/');
				if (file && file > m + 9 && !strstr (path, "/ww2/"))
				{
					char ww2[MAX_QPATH];
					Com_sprintf (ww2, sizeof(ww2), "%.*s/ww2%s", (int)(file - path), path, file);
					len = FS_FOpenFile (ww2, &h);
				}
			}
		}
	}

// look for it in the filesystem or pack files
	if (!h)
		len = FS_FOpenFile (path, &h);
	if (!h)
	{
		if (buffer)
			*buffer = NULL;
		return -1;
	}
	
	if (!buffer)
	{
		fclose (h);
		return len;
	}

	buf = Z_Malloc(len);
	*buffer = buf;

	FS_Read (buf, len, h);

	fclose (h);

	return len;
}


/*
=============
FS_FreeFile
=============
*/
void FS_FreeFile (void *buffer)
{
	Z_Free (buffer);
}

/*
=================
FS_LoadPackFile

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
pack_t *FS_LoadPackFile (char *packfile)
{
	dpackheader_t	header;
	int				i;
	packfile_t		*newfiles = NULL;
	int				numpackfiles;
	pack_t			*pack;
	FILE			*packhandle;
	dpackfile_t		*info = NULL;	// BDGN 3.51af: on the heap (was 4096 entries on the stack)
	unsigned		checksum;
	long			packlength;

	packhandle = fopen(packfile, "rb");
	if (!packhandle)
		return NULL;

	/* 3.60i: R1Q2/Yamagi review. Validate the real file boundaries before
	   allocating/using the directory; no extra package data is cached. */
	if (fseek (packhandle, 0, SEEK_END) ||
	    (packlength = ftell (packhandle)) < (long)sizeof(header) ||
	    fseek (packhandle, 0, SEEK_SET) ||
	    fread (&header, 1, sizeof(header), packhandle) != sizeof(header))
		goto invalid_pack;
	if (LittleLong(header.ident) != IDPAKHEADER)
		goto invalid_pack;
	header.dirofs = LittleLong (header.dirofs);
	header.dirlen = LittleLong (header.dirlen);
	if (header.dirofs < (int)sizeof(header) || header.dirlen < 0 ||
	    header.dirofs > packlength || header.dirlen > packlength-header.dirofs ||
	    header.dirlen % sizeof(dpackfile_t))
		goto invalid_pack;

	numpackfiles = header.dirlen / sizeof(dpackfile_t);

	/* BDGN 3.51af: limit raised from 4096 (MAX_FILES_IN_PACK) to 65536; the directory is read into heap memory */
	if (numpackfiles < 0 || numpackfiles > BDGN_MAX_FILES_IN_PACK)
		goto invalid_pack;

	newfiles = Z_Malloc (numpackfiles * sizeof(packfile_t));
	info = Z_Malloc (numpackfiles * sizeof(dpackfile_t) + 1);

	if (fseek (packhandle, header.dirofs, SEEK_SET) ||
	    fread (info, 1, header.dirlen, packhandle) != (size_t)header.dirlen)
		goto invalid_pack;

// crc the directory to check for modifications
	checksum = Com_BlockChecksum ((void *)info, numpackfiles * sizeof(dpackfile_t));

#ifdef NO_ADDONS
	if (checksum != PAK0_CHECKSUM)
	{
		Z_Free (info);
		Z_Free (newfiles);
		fclose (packhandle);
		return NULL;
	}
#endif
// parse the directory
	for (i=0 ; i<numpackfiles ; i++)
	{
		int filepos = LittleLong(info[i].filepos);
		int filelen = LittleLong(info[i].filelen);
		if (filepos < 0 || filelen < 0 || filepos > packlength ||
		    filelen > packlength-filepos)
			goto invalid_pack;
		/* BDGN 3.51ar: the 56-byte name need not end with a 0 (malformed PAK); id's strcpy then ran into the next
		   entries and past the 64-byte packfile_t name */
		memcpy (newfiles[i].name, info[i].name, sizeof(info[i].name));
		newfiles[i].name[sizeof(info[i].name)] = 0;
		newfiles[i].filepos = filepos;
		newfiles[i].filelen = filelen;
	}
	Z_Free (info);

	pack = Z_Malloc (sizeof (pack_t));
	strcpy (pack->filename, packfile);
	pack->handle = packhandle;
	pack->numfiles = numpackfiles;
	pack->files = newfiles;
	if (!FS_PackSorted (newfiles, numpackfiles))	// BDGN 3.51ar: index once at load (pak0: 3307 entries, a few ms)
	{
		pack->index = Z_Malloc (numpackfiles * sizeof(int));
		FS_PackBuildIndex (newfiles, numpackfiles, pack->index);
	}
	
	Com_Printf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;

invalid_pack:
	if (info) Z_Free (info);
	if (newfiles) Z_Free (newfiles);
	fclose (packhandle);
	Com_Error (ERR_FATAL, "%s has invalid or truncated PAK data", packfile);
	return NULL;
}


/*
================
FS_AddGameDirectory

Sets fs_gamedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ... 
================
*/
/*
================
BDGN 3.55n: quality packs in sub folders

baseq2/sounds/<any folder>/pak7-*.pak and baseq2/textures/<any folder>/{lowtex-*.pak, pak8-hd*-*.pak}. The folder
names are free; a pack is recognised by its file name, as before. FS_RescanQualityPacks runs again when the Video
or sound menu is opened, so a texture or sound set copied in while the game runs is found and becomes selectable.
================
*/
static char fs_quality_dir[MAX_OSPATH];

static void FS_CopyName (char *dst,const char *src,int size)
{
 strncpy(dst,src,size-1);
 dst[size-1]=0;
}

static const char *FS_BaseName (const char *path)
{
 const char *a=strrchr(path,'/'),*b=strrchr(path,'\\');
 if (b>a) a=b;
 return a?a+1:path;
}

static qboolean FS_PackLoadedByName (const char *name)
{
 searchpath_t *s;
 for (s=fs_searchpaths;s;s=s->next)
  if (s->pack && !Q_stricmp((char *)FS_BaseName(s->pack->filename),(char *)name)) return true;
 return false;
}

static qboolean FS_IsQualityPack (const char *name,qboolean textures)
{
 if (textures) {
  if (!Q_stricmp((char *)name,LOWTEX_SMALL_PAK) || !Q_stricmp((char *)name,LOWTEX_ULTRA_PAK)) return true;
  if (FS_FX4KPack(name)) return !Cvar_VariableValue("bdgn_legacy_graphics");	/* BDGN 3.56r */
  if (!Q_stricmp((char *)name,HDFX_PAK) || !Q_stricmp((char *)name,HDFX3D_PAK) || !Q_stricmp((char *)name,HDFX3DSD_PAK) || !Q_stricmp((char *)name,HDGORE_PAK)) return !Cvar_VariableValue("bdgn_legacy_graphics");
  return FS_HDSetOfPack(name)!=0 && !Cvar_VariableValue("bdgn_legacy_graphics");
 }
 return !Q_stricmp((char *)name,LOWSND_PAK) || !Q_stricmp((char *)name,LOWSND_4_PAK) || !Q_stricmp((char *)name,HDSND_PAK);
}

static int FS_ScanQualityPacks (const char *dir)
{
 char pattern[MAX_OSPATH],folders[32][MAX_QPATH],paks[64][MAX_OSPATH];
 char *found;
 int kind,nf,np,i,added=0;
 searchpath_t *search;
 pack_t *pak;
 if (dir!=fs_quality_dir) FS_CopyName(fs_quality_dir,dir,sizeof(fs_quality_dir));
 for (kind=0;kind<2;kind++) {
  const char *top=kind?"textures":"sounds";
  nf=0;
  Com_sprintf(pattern,sizeof(pattern),"%s/%s/*.*",dir,top);
  for (found=Sys_FindFirst(pattern,SFF_SUBDIR,SFF_HIDDEN|SFF_SYSTEM);found && nf<32;found=Sys_FindNext(SFF_SUBDIR,SFF_HIDDEN|SFF_SYSTEM)) {
   const char *name=FS_BaseName(found);
   if (!strcmp(name,".") || !strcmp(name,"..")) continue;
   FS_CopyName(folders[nf++],name,MAX_QPATH);
  }
  Sys_FindClose();
  for (i=0;i<nf;i++) {
   int j;
   np=0;
   Com_sprintf(pattern,sizeof(pattern),"%s/%s/%s/*.pak",dir,top,folders[i]);
   for (found=Sys_FindFirst(pattern,0,SFF_SUBDIR|SFF_HIDDEN|SFF_SYSTEM);found && np<64;found=Sys_FindNext(0,SFF_SUBDIR|SFF_HIDDEN|SFF_SYSTEM))
    FS_CopyName(paks[np++],found,MAX_OSPATH);
   Sys_FindClose();
   {	/* BDGN 3.57c: load in name order on every file system. NTFS returns names sorted, FAT (Windows 95/98/ME)
	   in the order they were written; each pak goes in front of the last, so the last name wins, and an extra
	   set such as pak8-hdsd-warp.pak must come after pak8-hdsd.pak to replace its water and lava */
    int a,b;
    char tmp[MAX_OSPATH];
    for (a=1;a<np;a++)
     for (b=a;b>0 && Q_stricmp((char *)FS_BaseName(paks[b-1]),(char *)FS_BaseName(paks[b]))>0;b--) {
      memcpy(tmp,paks[b],MAX_OSPATH);
      memcpy(paks[b],paks[b-1],MAX_OSPATH);
      memcpy(paks[b-1],tmp,MAX_OSPATH);
     }
   }
   for (j=0;j<np;j++) {
    const char *name=FS_BaseName(paks[j]);
    if (!FS_IsQualityPack(name,kind==1) || FS_PackLoadedByName(name)) continue;
    pak=FS_LoadPackFile(paks[j]);
    if (!pak) continue;
    search=Z_Malloc(sizeof(searchpath_t));
    search->pack=pak;
    search->next=fs_searchpaths;
    fs_searchpaths=search;
    added++;
   }
  }
 }
 return added;
}

/* called by the Video and sound menus: picks up texture / sound sets copied in after the start */
int FS_RescanQualityPacks (void)
{
 if (!fs_quality_dir[0]) return 0;
 return FS_ScanQualityPacks(fs_quality_dir);
}

void FS_AddGameDirectory (char *dir)
{
	int				i;
	searchpath_t	*search;
	pack_t			*pak;
	char			pakfile[MAX_OSPATH];

	FS_MissCacheClear ();	/* BDGN 3.59f */
	strcpy (fs_gamedir, dir);

	//
	// add the directory to the search path
	//
	search = Z_Malloc (sizeof(searchpath_t));
	strcpy (search->filename, dir);
	search->next = fs_searchpaths;
	fs_searchpaths = search;

	//
	// add any pak files in the format pak0.pak pak1.pak, ...
	//
	for (i=0; i<10; i++)
	{
		Com_sprintf (pakfile, sizeof(pakfile), "%s/pak%i.pak", dir, i);
		pak = FS_LoadPackFile (pakfile);
		if (!pak)
			continue;
		search = Z_Malloc (sizeof(searchpath_t));
		search->pack = pak;
		search->next = fs_searchpaths;
		fs_searchpaths = search;		
	}

	//

 for (i=0; i<(HD_SET_COUNT-1)*6 && !Cvar_VariableValue("bdgn_legacy_graphics"); i++) {
 Com_sprintf(pakfile,sizeof(pakfile),"%s/%s%s.pak",dir,hd_set_prefix[1+i/6],hd_pack_parts[i%6]);
 pak=FS_LoadPackFile(pakfile);	/* 3.55n: old place directly in baseq2 */
 if(pak) {
  search=Z_Malloc(sizeof(searchpath_t));
  search->pack=pak;
  search->next=fs_searchpaths;
  fs_searchpaths=search;
 }
 }

 /* Optional BDGN sprite bundle: one indexed asset file, no numbered-PAK collision. */
 Com_sprintf(pakfile,sizeof(pakfile),"%s/bdgn_sprites.pak",dir);
 pak=FS_LoadPackFile(pakfile);
 if(pak) {
  search=Z_Malloc(sizeof(searchpath_t));search->pack=pak;
  search->next=fs_searchpaths;fs_searchpaths=search;
 }
 /* BDGN 3.57l: simpler 3D models in three tiers (models/simple1..3/), used when cl_simplemodels is 1-3 (FS_LoadFile) */
 Com_sprintf(pakfile,sizeof(pakfile),"%s/bdgn_simplemodels.pak",dir);
 pak=FS_LoadPackFile(pakfile);
 if(pak) {
  search=Z_Malloc(sizeof(searchpath_t));search->pack=pak;
  search->next=fs_searchpaths;fs_searchpaths=search;
 }
 /* BDGN 3.57o: simpler maps for the renderer only (maps/simple/), used when cl_simpleworld is 1 (win32/vid_dll.c) */
 Com_sprintf(pakfile,sizeof(pakfile),"%s/bdgn_simpleworld.pak",dir);
 pak=FS_LoadPackFile(pakfile);
 if(pak) {
  search=Z_Malloc(sizeof(searchpath_t));search->pack=pak;
  search->next=fs_searchpaths;fs_searchpaths=search;
 }
 /* BDGN 3.52h: bdgn_anim.pak (64x64 animation library) is no longer loaded */
 /* BDGN 3.51q: 32x32 animation library (sprites/anim32/) and first-person weapon sprites (sprites/weap/); both optional */
 /* BDGN 3.58w: and the retro weapon look (sprites/weap/doom/, sprites/weap/wolf/; cl_retroweapons), optional */
 for(i=0;i<4;i++) {	/* 3.58y: and the enemy skin sets (bdgn_enemyskins.pak, cl_enemyskins), optional */
  Com_sprintf(pakfile,sizeof(pakfile),"%s/%s",dir,i==3?"bdgn_enemyskins.pak":i==2?"bdgn_weapons_retro.pak":i?"bdgn_weapons.pak":Cvar_VariableValue("bdgn_extra_lowram")?"bdgn_anim32_fb.pak":"bdgn_anim32.pak");
  pak=FS_LoadPackFile(pakfile);
  if(pak) {search=Z_Malloc(sizeof(searchpath_t));search->pack=pak;search->next=fs_searchpaths;fs_searchpaths=search;}
 }


	// BDGN 3.32: the optional low quality packs; added last so they are searched first
	// Texture and sound settings determine which of these packs is searched.
	// BDGN 3.55n: the packs live in baseq2/textures/<pack>/ and baseq2/sounds/<pack>/ (FS_ScanQualityPacks);
	// the old place directly in baseq2 still works.
	//
	FS_ScanQualityPacks (dir);
	for (i=0; i<9; i++)	/* 3.55y: 9 (pak8-hdgore.pak) */
	{
		static const char *quality_packs[] = { LOWTEX_SMALL_PAK, LOWTEX_ULTRA_PAK, LOWSND_PAK, LOWSND_4_PAK, HDSND_PAK, HDFX_PAK, HDFX3D_PAK, HDFX3DSD_PAK, HDGORE_PAK };
		if (FS_PackLoadedByName (quality_packs[i]))
			continue;	/* 3.55n: already found in a sounds/ or textures/ folder */
		if (i >= 5 && Cvar_VariableValue ("bdgn_legacy_graphics"))
			continue;	/* 3.55y: the HD effect packs are not even opened on legacy computers (as in textures/) */
		Com_sprintf (pakfile, sizeof(pakfile), "%s/%s", dir, quality_packs[i]);
		pak = FS_LoadPackFile (pakfile);
		if (!pak)
			continue;
		search = Z_Malloc (sizeof(searchpath_t));
		search->pack = pak;
		search->next = fs_searchpaths;
		fs_searchpaths = search;
	}

}

/*
============
FS_Gamedir

Called to find where to write a file (demos, savegames, etc)
============
*/
char *FS_Gamedir (void)
{
	if (*fs_gamedir)
		return fs_gamedir;
	else
		return BASEDIRNAME;
}

/*
=============
FS_ExecAutoexec
=============
*/
void FS_ExecAutoexec (void)
{
	char *dir;
	char name [MAX_QPATH];

	dir = Cvar_VariableString("gamedir");
	if (*dir)
		Com_sprintf(name, sizeof(name), "%s/%s/autoexec.cfg", fs_basedir->string, dir); 
	else
		Com_sprintf(name, sizeof(name), "%s/%s/autoexec.cfg", fs_basedir->string, BASEDIRNAME); 
	if (Sys_FindFirst(name, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM))
		Cbuf_AddText ("exec autoexec.cfg\n");
	Sys_FindClose();
}


/*
================
FS_SetGamedir

Sets the gamedir and path to a different directory.
================
*/
void FS_SetGamedir (char *dir)
{
	searchpath_t	*next;

	FS_MissCacheClear ();	/* BDGN 3.59f */

	if (strstr(dir, "..") || strstr(dir, "/")
		|| strstr(dir, "\\") || strstr(dir, ":") )
	{
		Com_Printf ("Gamedir should be a single filename, not a path\n");
		return;
	}

	//
	// free up any current game dir info
	//
	while (fs_searchpaths != fs_base_searchpaths)
	{
		if (fs_searchpaths->pack)
		{
			fclose (fs_searchpaths->pack->handle);
			Z_Free (fs_searchpaths->pack->files);
			if (fs_searchpaths->pack->index)
				Z_Free (fs_searchpaths->pack->index);
			Z_Free (fs_searchpaths->pack);
		}
		next = fs_searchpaths->next;
		Z_Free (fs_searchpaths);
		fs_searchpaths = next;
	}

	//
	// flush all data, so it will be forced to reload
	//
	if (dedicated && !dedicated->value)
		Cbuf_AddText ("vid_restart\nsnd_restart\n");

	Com_sprintf (fs_gamedir, sizeof(fs_gamedir), "%s/%s", fs_basedir->string, dir);

	if (!strcmp(dir,BASEDIRNAME) || (*dir == 0))
	{
		Cvar_FullSet ("gamedir", "", CVAR_SERVERINFO|CVAR_NOSET);
		Cvar_FullSet ("game", "", CVAR_LATCH|CVAR_SERVERINFO);
	}
	else
	{
		Cvar_FullSet ("gamedir", dir, CVAR_SERVERINFO|CVAR_NOSET);
		if (fs_cddir->string[0])
			FS_AddGameDirectory (va("%s/%s", fs_cddir->string, dir) );
		FS_AddGameDirectory (va("%s/%s", fs_basedir->string, dir) );
	}
}


/*
================
FS_Link_f

Creates a filelink_t
================
*/
void FS_Link_f (void)
{
	filelink_t	*l, **prev;

	FS_MissCacheClear ();	/* BDGN 3.59f */
	if (Cmd_Argc() != 3)
	{
		Com_Printf ("USAGE: link <from> <to>\n");
		return;
	}

	// see if the link already exists
	prev = &fs_links;
	for (l=fs_links ; l ; l=l->next)
	{
		if (!strcmp (l->from, Cmd_Argv(1)))
		{
			Z_Free (l->to);
			if (!strlen(Cmd_Argv(2)))
			{	// delete it
				*prev = l->next;
				Z_Free (l->from);
				Z_Free (l);
				return;
			}
			l->to = CopyString (Cmd_Argv(2));
			return;
		}
		prev = &l->next;
	}

	// create a new link
	l = Z_Malloc(sizeof(*l));
	l->next = fs_links;
	fs_links = l;
	l->from = CopyString(Cmd_Argv(1));
	l->fromlength = strlen(l->from);
	l->to = CopyString(Cmd_Argv(2));
}

/*
** FS_ListFiles
*/
char **FS_ListFiles( char *findname, int *numfiles, unsigned musthave, unsigned canthave )
{
	char *s;
	int nfiles = 0;
	char **list = 0;

	s = Sys_FindFirst( findname, musthave, canthave );
	while ( s )
	{
		if ( s[strlen(s)-1] != '.' )
			nfiles++;
		s = Sys_FindNext( musthave, canthave );
	}
	Sys_FindClose ();

	if ( !nfiles )
		return NULL;

	nfiles++; // add space for a guard
	*numfiles = nfiles;

	list = malloc( sizeof( char * ) * nfiles );
	memset( list, 0, sizeof( char * ) * nfiles );

	s = Sys_FindFirst( findname, musthave, canthave );
	nfiles = 0;
	while ( s )
	{
		if ( s[strlen(s)-1] != '.' )
		{
			list[nfiles] = strdup( s );
#ifdef _WIN32
			strlwr( list[nfiles] );
#endif
			nfiles++;
		}
		s = Sys_FindNext( musthave, canthave );
	}
	Sys_FindClose ();

	return list;
}

/*
** FS_Dir_f
*/
void FS_Dir_f( void )
{
	char	*path = NULL;
	char	findname[1024];
	char	wildcard[1024] = "*.*";
	char	**dirnames;
	int		ndirs;

	if ( Cmd_Argc() != 1 )
	{
		strcpy( wildcard, Cmd_Argv( 1 ) );
	}

	while ( ( path = FS_NextPath( path ) ) != NULL )
	{
		char *tmp = findname;

		Com_sprintf( findname, sizeof(findname), "%s/%s", path, wildcard );

		while ( *tmp != 0 )
		{
			if ( *tmp == '\\' ) 
				*tmp = '/';
			tmp++;
		}
		Com_Printf( "Directory of %s\n", findname );
		Com_Printf( "----\n" );

		if ( ( dirnames = FS_ListFiles( findname, &ndirs, 0, 0 ) ) != 0 )
		{
			int i;

			for ( i = 0; i < ndirs-1; i++ )
			{
				if ( strrchr( dirnames[i], '/' ) )
					Com_Printf( "%s\n", strrchr( dirnames[i], '/' ) + 1 );
				else
					Com_Printf( "%s\n", dirnames[i] );

				free( dirnames[i] );
			}
			free( dirnames );
		}
		Com_Printf( "\n" );
	};
}

/*
============
FS_Path_f

============
*/
void FS_Path_f (void)
{
	searchpath_t	*s;
	filelink_t		*l;

	Com_Printf ("Current search path:\n");
	for (s=fs_searchpaths ; s ; s=s->next)
	{
		if (s == fs_base_searchpaths)
			Com_Printf ("----------\n");
		if (s->pack)
			Com_Printf ("%s (%i files)\n", s->pack->filename, s->pack->numfiles);
		else
			Com_Printf ("%s\n", s->filename);
	}

	Com_Printf ("\nLinks:\n");
	for (l=fs_links ; l ; l=l->next)
		Com_Printf ("%s : %s\n", l->from, l->to);
}

/*
================
FS_NextPath

Allows enumerating all of the directories in the search path
================
*/
char *FS_NextPath (char *prevpath)
{
	searchpath_t	*s;
	char			*prev;

	if (!prevpath)
		return fs_gamedir;

	prev = fs_gamedir;
	for (s=fs_searchpaths ; s ; s=s->next)
	{
		if (s->pack)
			continue;
		if (prevpath == prev)
			return s->filename;
		prev = s->filename;
	}

	return NULL;
}


/*
================
FS_InitFilesystem
================
*/
void FS_InitFilesystem (void)
{
	Cmd_AddCommand ("path", FS_Path_f);
	Cmd_AddCommand ("link", FS_Link_f);
	Cmd_AddCommand ("dir", FS_Dir_f );

	//
	// basedir <path>
	// allows the game to run from outside the data tree
	//
	fs_basedir = Cvar_Get ("basedir", ".", CVAR_NOSET);

	// BDGN 3.32: low quality packs (off = identical to earlier versions)
	fs_lowtextures = Cvar_Get ("fs_lowtextures", "0", CVAR_ARCHIVE);
	fs_sound_rate = Cvar_Get("s_khz","11",CVAR_ARCHIVE);

	//
	// cddir <path>
	// Logically concatenates the cddir after the basedir for 
	// allows the game to run from outside the data tree
	//
	fs_cddir = Cvar_Get ("cddir", "", CVAR_NOSET);
	if (fs_cddir->string[0])
		FS_AddGameDirectory (va("%s/"BASEDIRNAME, fs_cddir->string) );

	//
	// start up with baseq2 by default
	//
	FS_AddGameDirectory (va("%s/"BASEDIRNAME, fs_basedir->string) );

	// any set gamedirs will be freed up to here
	fs_base_searchpaths = fs_searchpaths;

	// check for game override
	fs_gamedirvar = Cvar_Get ("game", "", CVAR_LATCH|CVAR_SERVERINFO);
	if (fs_gamedirvar->string[0])
		FS_SetGamedir (fs_gamedirvar->string);
}

/*
BDGN 3.59z (Q2PRO): file names for the console's completion (map and demo names). Calls found() with each name below
dir that starts with partial and ends with ext (without dir and ext), from the paks and the folders of every search
path; returns how many. A name can come more than once (several paks); the caller sorts that out.
*/
int FS_CompleteFiles (const char *dir, const char *ext, const char *partial, void (*found) (const char *name))
{
	searchpath_t *search;
	char prefix[MAX_QPATH], pattern[MAX_OSPATH], name[MAX_QPATH], *f;
	int i, n = 0, plen, elen = (int)strlen (ext);
	Com_sprintf (prefix, sizeof(prefix), "%s/%s", dir, partial);
	plen = (int)strlen (prefix);
	for (search = fs_searchpaths; search; search = search->next)
	{
		if (search->pack)
		{
			pack_t *pak = search->pack;
			for (i = 0; i < pak->numfiles; i++)
			{
				const char *fn = pak->files[i].name;
				int len = (int)strlen (fn);
				if (len <= plen || len - elen <= (int)strlen (dir) + 1 || Q_strncasecmp ((char *)fn, prefix, plen)
					|| Q_stricmp ((char *)fn + len - elen, (char *)ext) || strchr (fn + strlen (dir) + 1, '/'))
					continue;
				strncpy (name, fn + strlen (dir) + 1, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
				name[strlen (name) - elen] = 0;
				found (name);
				n++;
			}
		}
		else
		{
			Com_sprintf (pattern, sizeof(pattern), "%s/%s/%s*%s", search->filename, dir, partial, ext);
			for (f = Sys_FindFirst (pattern, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM); f; f = Sys_FindNext (0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM))
			{
				const char *base = strrchr (f, '/');
				base = base ? base + 1 : f;
				strncpy (name, base, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
				if ((int)strlen (name) > elen)
				{
					name[strlen (name) - elen] = 0;
					found (name);
					n++;
				}
			}
			Sys_FindClose ();
		}
	}
	return n;
}

