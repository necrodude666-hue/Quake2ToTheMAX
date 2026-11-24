/* Pre-rendered first-person weapon banks from sprites/weap. */
#ifndef BDGN_WEAPON_SPRITE_H
#define BDGN_WEAPON_SPRITE_H

#include "duke_anim.h"

#define WS_MAGIC 0x53574442
typedef struct {
	int		magic, version, frames, width, height;
	float	u0, v1, pix, depth;
	int		idle;
	float	flash_u, flash_v;
	int		size, pad[3];
} ws_header_t;
typedef struct { unsigned offset, packed; short occ[4]; } ws_record_t;

static const ws_header_t *WS_ValidateBank (const byte *data, int size)
{
 const ws_header_t *h = (const ws_header_t *)data;
 if (!h || size < (int)sizeof(*h) || h->magic != WS_MAGIC || h->version != 1 ||
     h->frames < 1 || h->frames > 512 || h->width < 1 || h->width > 256 ||
     h->height < 1 || h->height > 256 || h->size != size ||
     (int)sizeof(*h) + h->frames * (int)sizeof(ws_record_t) > size ||
     !(h->pix > 0 && h->pix <= 1) || !(h->depth > 0 && h->depth < 1024) ||
     !(h->u0 >= -8 && h->u0 <= 8 && h->v1 >= -8 && h->v1 <= 8))
  return NULL;
 return h;
}

/* BDGN 3.58w (owner 2026-11-21): the retro weapon look (bdgn_weapons_retro.pak, _extra/weapon_sprites/retro_weapons.py,
   made from these banks): cl_retroweapons 1 draws sprites/weap/doom/ (pixels twice as large) or, in Wolfenstein 3D
   mode (cl_retromode 2/3), sprites/weap/wolf/ (three times as large, 32 flat colours, half the animation pictures).
   A missing retro bank falls back to the normal one. WS_RetroStyle: 0 normal, 1 Doom, 2 Wolfenstein 3D. */
static int WS_RetroStyle (void)
{
 static cvar_t *rw, *rm;
 if (!rw) { rw = ri.Cvar_Get ("cl_retroweapons", "0", CVAR_ARCHIVE); rm = ri.Cvar_Get ("cl_retromode", "0", CVAR_ARCHIVE); }
 if (!rw->value) return 0;
 return (rm->value == 2 || rm->value == 3) ? 2 : 1;	/* 3.60i: Duke Nukem 3D mode (4) has the Doom look */
}
static int WS_LoadBankFile (const char *v, int n, void **data)
{
 char path[MAX_QPATH];
 int style = WS_RetroStyle (), size = -1;
 if (style) {
  Com_sprintf (path, sizeof(path), "sprites/weap/%s/%.*s.dwa", style == 2 ? "wolf" : "doom", n, v);
  size = ri.FS_LoadFile (path, data);
  if (*data) return size;
 }
 Com_sprintf (path, sizeof(path), "sprites/weap/%.*s.dwa", n, v);
 return ri.FS_LoadFile (path, data);
}

static int WS_DecodeFrame (const ws_header_t *h, int frame, byte *pixels)
{
 const byte *data = (const byte *)h;
 const ws_record_t *r = (const ws_record_t *)(data + sizeof(*h)) + frame;
 return r->offset <= (unsigned)h->size && r->packed <= (unsigned)h->size - r->offset &&
        Duke_Expand (data + r->offset, r->packed, pixels, h->width * h->height);
}

#endif
