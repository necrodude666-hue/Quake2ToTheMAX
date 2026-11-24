/*
gl_weapsprite.h -- BDGN 3.51q: first-person weapon as pre-rendered 2D sprites (cl_weaponsprites), Simple Weapon Mode
(cl_weapon_simple). Included once, by gl_mesh.c.

Visual only: the client still sends the real weapon model, frame, origin and angles; game, network, ammunition, damage,
projectiles and recoil are not touched. The client sets RF_WEAPONSPRITE on the view weapon entity when the option is
on and holds its frame on the 12.5 or 6.25 per second clock; RF_WEAPONSIMPLE draws the bank's idle picture only;
an extra entity with RF_WEAPONFLASH draws the shared muzzle flash at the weapon's flash point (3.51r: the client no
longer sends it). BDGN 3.51s: R_WeaponSpritesPreload (from R_EndRegistration) reads all eleven banks and uploads each
weapon's idle picture while the map loads, so switching weapons reads no file, and Simple Weapon Mode uploads nothing
during play (11 idle pictures in the 12 textures).

Banks (sprites/weap/<v_name>.dwa in bdgn_weapons.pak, _extra/weapon_sprites/weapgen.c): every frame of one view model
on one pixel grid in "tangent space" (u = right/forward, v = up/forward). Any point of the 3D model is seen at
direction (u, v) from the eye, so a quad spanning the bank's (u, v) rectangle at distance D in front of the weapon
origin, with the weapon angles, covers exactly the screen area of the 3D model at any fov; the bob offset is applied
at D (the mean depth of the model). Pixels are drawn with GL_NEAREST (no filtering), lit like the 3D weapon
(light at the weapon origin, RF_MINLIGHT) and depth-hacked like the 3D weapon.
Textures: a small LRU of 256x256 textures, one picture each (idle animations revisit the same pictures).

BDGN 3.52h: "skip world behind weapon" (cl_weapon_occlusion) removed: it cost 1.5% (K6-III+ bench, 3.52c);
the per-frame rectangle in the banks (ws_record_t.occ) is no longer read.
*/
void GL_LightScaleTexture (unsigned *in, int inwidth, int inheight, qboolean only_gamma);
#define WS_BANKS		16
#include "gl_weapcache.h"	/* BDGN 3.52 (WESTBECK): which pictures are kept as textures (memory budget gl_weapon_texmem) */
#define WS_TEXBASE		TEXNUM_WEAPSPRITES	/* BDGN 3.59i: 768-895, below the 2D enemy pages (gl_local.h); WSC_SLOTS (128) numbers */
#include "../qcommon/weapon_sprite.h"
typedef struct { char model[MAX_QPATH]; byte *data; int size, attempt; } ws_bank_t;
static ws_bank_t	ws_banks[WS_BANKS];
static int			ws_nbanks, ws_flashbank = -2;
static wsc_t			ws_cache;	/* BDGN 3.52 (WESTBECK); 3.51att-3.52u: 32 textures of 256x256, least recently used replaced */
static perf_cycles_t	ws_upload_cycles;	/* decode + convert + upload time of misses (weapon_stats) */
static unsigned		ws_draws, ws_uploads, ws_hits, ws_fallbacks, ws_flashes;
static cvar_t		*ws_cv_cache, *ws_cv_mem;
static byte			ws_pixels[256 * 256];
static unsigned		ws_rgba[256 * 256];

/* a cached picture's texture is given up to stay within the budget */
static void WS_FreeTexture (int slot)
{
	unsigned tex = WS_TEXBASE + slot;
	if (qglDeleteTextures) qglDeleteTextures (1, &tex);
	if (gl_state.currenttextures[0] == (int)tex) gl_state.currenttextures[0] = -1;
	if (gl_state.currenttextures[1] == (int)tex) gl_state.currenttextures[1] = -1;
}
/* texture memory in bytes: gl_weapon_texmem (MB, 2-32) when set; 0 (default) = from the old count gl_weapon_texcache
   (pictures of 256 KB: 32 = 8 MB, the S3 Virge / Rendition presets' 12 = 3 MB), so the presets need no change */
static int WS_Budget (void)
{
	float mb = ws_cv_mem ? ws_cv_mem->value : 0;
	int b;
	if (mb <= 0)
		mb = ( ws_cv_cache ? ws_cv_cache->value : 32 ) / 4.0f;
	b = (int)(mb * 1024 * 1024);
	if (b < 2 * 1024 * 1024) b = 2 * 1024 * 1024;
	if (b > 32 * 1024 * 1024) b = 32 * 1024 * 1024;
	if (R_LowVram ())	/* BDGN 3.59s: 4 MB video card (vid_lowvram): 1 MB, four pictures (TEORETISK ANALYS) */
		b = 1024 * 1024;
	return b;
}

void R_WeaponSpritesReset (void);
static int WS_Load (const char *name)
{
	ws_bank_t *b;
	int i, size;
	byte *data = NULL;
	const ws_header_t *h;
	{	/* BDGN 3.58w: another weapon look: the loaded banks and their pictures are let go once */
		static int last_style = -1;
		int style = WS_RetroStyle ();
		if (last_style >= 0 && style != last_style)
			R_WeaponSpritesReset ();
		last_style = style;
	}
	for (i = 0; i < ws_nbanks; i++)
		if (!strcmp (ws_banks[i].model, name))
			return ws_banks[i].data ? i : -1;
	if (ws_nbanks >= WS_BANKS)
		return -1;
	b = &ws_banks[ws_nbanks];
	memset (b, 0, sizeof(*b));
	strncpy (b->model, name, sizeof(b->model) - 1);
	{
		const char *v = strstr (name, "/v_");
		if (!strcmp (name, "flash"))
			size = WS_LoadBankFile ("flash", 5, (void **)&data);	/* BDGN 3.58w: retro look when chosen */
		else if (v) {
			int n = 0;
			v++;
			while (v[n] && v[n] != '/' && n < 24) n++;
			size = WS_LoadBankFile (v, n, (void **)&data);
		} else
			size = -1;
	}
	ws_nbanks++;
	if (!data)
		return -1;
	h = WS_ValidateBank (data, size);
	if (!h) {
		ri.Con_Printf (PRINT_ALL, "BDGN weapons: invalid bank for %s\n", name);
		ri.FS_FreeFile (data);
		return -1;
	}
	b->data = data; b->size = size;
	return ws_nbanks - 1;
}
void R_WeaponSpritesReset (void)
{
	int i;
	unsigned tex;
	for (i = 0; i < ws_nbanks; i++)
		if (ws_banks[i].data)
			ri.FS_FreeFile (ws_banks[i].data);
	ws_nbanks = 0; ws_flashbank = -2;
	(void)tex;
	for (i = 0; i < WSC_SLOTS; i++)
		if (ws_cache.s[i].texh)
			WS_FreeTexture (i);
	memset (&ws_cache, 0, sizeof(ws_cache));
	ws_draws = ws_uploads = ws_hits = ws_fallbacks = ws_flashes = 0;
	ws_upload_cycles = 0;
}
#include "../qcommon/minigl_compat.h"
static qboolean ws_driver_blocked;

/* support: 2 = sprites; 0 = unavailable in this renderer */
void R_WeaponSpritesInit (void)
{
	ws_cv_cache = ri.Cvar_Get ("gl_weapon_texcache", "32", CVAR_ARCHIVE);	/* BDGN 3.51att: pictures; used when gl_weapon_texmem is 0 */
	ws_cv_mem = ri.Cvar_Get ("gl_weapon_texmem", "0", CVAR_ARCHIVE);	/* BDGN 3.52 (WESTBECK): MB, 0 = from gl_weapon_texcache */
	ri.Cvar_Get ("r_weaponsprite_support", "0", 0);
	ri.Cvar_Get ("r_weapon_stats", "", 0);
	ws_driver_blocked=BDGN_GLUses3DWeapons(gl_driver->string,ri.Cvar_Get("bdgn_mtgl_target","",0)->string);
	if(ws_driver_blocked) {
		ri.Cvar_Get("cl_weaponsprites","0",CVAR_ARCHIVE);
		ri.Cvar_Get("cl_weapon_simple","0",CVAR_ARCHIVE);
		ri.Cvar_SetValue("cl_weaponsprites",0);
		ri.Cvar_SetValue("cl_weapon_simple",0);
		ri.Con_Printf(PRINT_ALL,"3dfx compatibility: using 3D view weapons.\n");
	}
	ri.Cvar_SetValue ("r_weaponsprite_support",
		!ws_driver_blocked && ri.FS_LoadFile ("sprites/weap/v_blast.dwa", NULL) > 0 ? 2 : 0);
	R_WeaponSpritesReset ();
}
static int WS_Texture (int bank, int frame);
/* BDGN 3.51s: automatic, at the end of map loading, whenever 2D weapons are on */
void R_WeaponSpritesPreload (void)
{
	static const char *names[] = { "v_blast", "v_shotg", "v_shotg2", "v_machn", "v_chain", "v_handgr", "v_launch",
		"v_rocket", "v_hyperb", "v_rail", "v_bfg" };
	char model[MAX_QPATH];
	int i, bank;
	cvar_t *on = ri.Cvar_Get ("cl_weaponsprites", "0", CVAR_ARCHIVE);
	if (ws_driver_blocked || (!on->value && !ri.Cvar_Get ("cl_classic2d", "0", CVAR_ARCHIVE)->value) ||
		ri.Cvar_Get ("r_weaponsprite_support", "0", 0)->value < 1)
		return;
	for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
		Com_sprintf (model, sizeof(model), "models/weapons/%s/tris.md2", names[i]);
		bank = WS_Load (model);
		if (bank >= 0 && ((const ws_header_t *)ws_banks[bank].data)->idle >= 0 && ((const ws_header_t *)ws_banks[bank].data)->idle < ((const ws_header_t *)ws_banks[bank].data)->frames)
			WS_Texture (bank, ((const ws_header_t *)ws_banks[bank].data)->idle);
	}
}
void R_WeaponStats (void)
{
	char line[320];
	double upload_ms;
	int i, tex = 0;
	if (ri.Cmd_Argc () > 1 && !strcmp (ri.Cmd_Argv (1), "reset")) { R_WeaponSpritesReset (); return; }
	if (ri.Cmd_Argc () > 1 && !strcmp (ri.Cmd_Argv (1), "zero")) {	/* BDGN 3.51att: counters only (bdgn_bench, every run) */
		ws_draws = ws_uploads = ws_hits = ws_fallbacks = ws_flashes = 0;
		ws_upload_cycles = 0;
		return;
	}
	for (i = 0; i < WSC_SLOTS; i++) if (ws_cache.s[i].valid) tex++;
	{	/* BDGN 3.52 (WESTBECK): time of the misses (decode, convert, upload); -1 = no cycle counter */
		double cpm = R_PerfCyclesPerMs ();
		upload_ms = cpm > 0 ? (double)ws_upload_cycles / cpm : -1;
	}
	Com_sprintf (line, sizeof(line), "draws %u hits %u uploads %u upload_ms %.1f each_ms %.2f flashes %u fallbacks %u pictures %i texKB %i budgetKB %i banks %i",
		ws_draws, ws_hits, ws_uploads, upload_ms, upload_ms >= 0 && ws_uploads ? upload_ms / ws_uploads : -1.0, ws_flashes, ws_fallbacks, tex, ws_cache.bytes / 1024, WS_Budget () / 1024, ws_nbanks);
	ri.Cvar_Set ("r_weapon_stats", line);
	if (ri.Cmd_Argc () > 1 && !strcmp (ri.Cmd_Argv (1), "quiet")) return;
	ri.Con_Printf (PRINT_ALL, "BDGN 2D weapon: %s\n", line);
	ri.Con_Printf (PRINT_ALL, "  (upload_ms = time of all uploads, each_ms = per upload; pictures cached, texKB = their texture memory, budgetKB = gl_weapon_texmem)\n");
}

/* the bank and frame an entity shows; frame -1 = none */
static int WS_Frame (entity_t *e, int *bank)
{
	const ws_header_t *h;
	int f;
	if (e->flags & RF_WEAPONFLASH) {
		if (ws_flashbank == -2) ws_flashbank = WS_Load ("flash");
		*bank = ws_flashbank;
		return *bank >= 0 ? 0 : -1;
	}
	*bank = WS_Load (currentmodel->name);
	if (*bank < 0) return -1;
	h = (const ws_header_t *)ws_banks[*bank].data;
	f = (e->flags & RF_WEAPONSIMPLE) ? h->idle : e->frame;
	if (f < 0 || f >= h->frames) f = 0;
	return f;
}
static int WS_Texture (int bank, int frame)
{
	const ws_header_t *h = (const ws_header_t *)ws_banks[bank].data;
	int i, slot, create, n = h->width * h->height;
	unsigned pal[256];
	perf_cycles_t c0;
	slot = WSC_Find (&ws_cache, bank, frame);
	if (slot >= 0) { ws_hits++; return slot; }
	c0 = Q3S_Cycles ();	/* BDGN 3.52 (WESTBECK): weapon_stats upload time */
	if (!WS_DecodeFrame (h, frame, ws_pixels))
		return -1;
	slot = WSC_Place (&ws_cache, bank, frame, frame == h->idle, h->height, WS_Budget (), WS_FreeTexture, &create);
	if (slot < 0)
		return -1;
	/* BDGN 3.52 (WESTBECK): one pass. Gamma is a per-channel table, so it is applied to the 256 palette colours
	   instead of every pixel (3.51q-3.52u: palette pass, then GL_LightScaleTexture over the whole picture) */
	for (i = 0; i < 256; i++) pal[i] = d_8to24table[i];
	GL_LightScaleTexture (pal, 256, 1, true);
	pal[255] &= 0x00ffffff;
	for (i = 0; i < n; i++) ws_rgba[i] = pal[ws_pixels[i]];
	GL_SelectTexture (GL_TEXTURE0);
	GL_Bind (WS_TEXBASE + slot);
	if (create || !qglTexSubImage2D) {
		int y, height = ws_cache.s[slot].texh;
		/* Expand to the padded texture in place, working back from the last row. */
		for (y = h->height - 1; y >= 0; y--) {
			memmove (ws_rgba + y * 256, ws_rgba + y * h->width, h->width * sizeof(ws_rgba[0]));
			memset (ws_rgba + y * 256 + h->width, 0, (256 - h->width) * sizeof(ws_rgba[0]));
		}
		memset (ws_rgba + h->height * 256, 0, (height - h->height) * 256 * sizeof(ws_rgba[0]));
		qglTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, 256, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, ws_rgba);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);	/* visible pixels, no smoothing */
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	} else {
		qglTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, h->width, h->height, GL_RGBA, GL_UNSIGNED_BYTE, ws_rgba);
	}
	ws_uploads++;
	{ perf_cycles_t c1 = Q3S_Cycles (); if (c0 && c1 > c0) ws_upload_cycles += c1 - c0; }
	return slot;
}
/* world point of tangent position (u, v) on the weapon's quad */
static void WS_Point (const entity_t *e, const vec3_t f, const vec3_t r, const vec3_t up, float depth, float u, float v, vec3_t out)
{
	int k;
	for (k = 0; k < 3; k++) out[k] = e->origin[k] + depth * (f[k] + u * r[k] + v * up[k]);
}
static int R_DrawWeaponSprite (entity_t *e)
{
	const ws_header_t *h, *wh = NULL;
	vec3_t f, r, up, p;
	float u0, u1, v0, v1, s1, t1, light[3], depth;
	int bank, frame, slot, k, mirror = r_lefthand->value == 1;
	if(ws_driver_blocked) return (e->flags & RF_WEAPONFLASH) != 0;
	frame = WS_Frame (e, &bank);
	if (frame < 0) { ws_fallbacks++; return (e->flags & RF_WEAPONFLASH) ? 1 : 0; }	/* no bank: 3D weapon; no flash bank: nothing */
	if (r_lefthand->value == 2) return 1;
	h = (const ws_header_t *)ws_banks[bank].data;
	slot = WS_Texture (bank, frame);
	if (slot < 0) { ws_fallbacks++; return (e->flags & RF_WEAPONFLASH) ? 1 : 0; }
	AngleVectors (e->angles, f, r, up);
	u0 = h->u0; v1 = h->v1; u1 = u0 + h->width * h->pix; v0 = v1 - h->height * h->pix; depth = h->depth;
	if (e->flags & RF_WEAPONFLASH) {	/* centred on the weapon's flash point, a little in front of the weapon */
		int wb = WS_Load (currentmodel->name);
		if (wb < 0) return 1;
		wh = (const ws_header_t *)ws_banks[wb].data;
		u1 -= u0; v0 -= v1; u0 = wh->flash_u - u1 * 0.5f; v1 = wh->flash_v - v0 * 0.5f; u1 += u0; v0 += v1;
		depth = wh->depth * 0.9f; ws_flashes++;
	}
	if (mirror) { float t = -u1; u1 = -u0; u0 = t; }
	s1 = h->width / 256.0f; t1 = h->height / (float)ws_cache.s[slot].texh;	/* BDGN 3.52 (WESTBECK): texture as tall as needed */
	if ((e->flags & (RF_FULLBRIGHT | RF_WEAPONFLASH)) || GL_FULLBRIGHT ())
		light[0] = light[1] = light[2] = 1;
	else {
		R_LightPoint (e->origin, light);
		for (k = 0; k < 3; k++) { if (light[k] < 0.1f) light[k] = 0.1f; if (light[k] > 1) light[k] = 1; }	/* RF_MINLIGHT */
	}
	qglDepthRange (gldepthmin, gldepthmin + 0.3 * (gldepthmax - gldepthmin));
	qglDisable (GL_BLEND);
	qglColor4f (light[0], light[1], light[2], 1);
	GL_SelectTexture (GL_TEXTURE0);
	GL_Bind (WS_TEXBASE + slot);
	GL_TexEnv (GL_MODULATE);
	qglEnable (GL_ALPHA_TEST);
	qglBegin (GL_QUADS);
	qglTexCoord2f (mirror ? s1 : 0, t1); WS_Point (e, f, r, up, depth, u0, v0, p); qglVertex3fv (p);
	qglTexCoord2f (mirror ? s1 : 0, 0);  WS_Point (e, f, r, up, depth, u0, v1, p); qglVertex3fv (p);
	qglTexCoord2f (mirror ? 0 : s1, 0);  WS_Point (e, f, r, up, depth, u1, v1, p); qglVertex3fv (p);
	qglTexCoord2f (mirror ? 0 : s1, t1); WS_Point (e, f, r, up, depth, u1, v0, p); qglVertex3fv (p);
	qglEnd ();
	qglDisable (GL_ALPHA_TEST);
	qglColor4f (1, 1, 1, 1);
	qglDepthRange (gldepthmin, gldepthmax);
	ws_draws++;
	return 1;
}
