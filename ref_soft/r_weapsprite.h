/* First-person sprites use the Software view buffer before water warp. */
#include "../qcommon/weapon_sprite.h"

static const ws_header_t *weapon_bank;
static char weapon_model[MAX_QPATH];
static byte weapon_pixels[256 * 256];
static int weapon_frame = -1;
static qboolean weapon_ready;
static vec3_t weapon_corners[4], weapon_forward, weapon_s_axis, weapon_t_axis;
static qboolean weapon_mirror;

static void SW_WeaponSpritesReset (void)
{
 if (weapon_bank)
  ri.FS_FreeFile ((void *)weapon_bank);
 weapon_bank = NULL;
 weapon_model[0] = 0;
 weapon_frame = -1;
 ri.Cvar_SetValue ("r_weaponsprite_support", 0);
}

void SW_WeaponSpritesInit (void)
{
 SW_WeaponSpritesReset ();
 ri.Cvar_SetValue ("r_weaponsprite_support",
                   ri.FS_LoadFile ("sprites/weap/v_blast.dwa", NULL) > 0 ? 1 : 0);
}

static qboolean SW_WeaponSpritePrepare (entity_t *e, const char *model)
{
 const ws_header_t *h;
 vec3_t right, up, center, point;
 float u, v, unit, left;
 int frame, row, col, corner;
 qboolean mirror = r_lefthand->value == 1;

 if (r_lefthand->value == 2)
  return true;
 {	/* BDGN 3.58w: another weapon look loads the bank again */
  static int last_style = -1;
  int style = WS_RetroStyle ();
  if (last_style >= 0 && style != last_style) weapon_model[0] = 0;
  last_style = style;
 }
 if (strcmp (weapon_model, model))
 {
  char path[MAX_QPATH];
  const char *name = strstr (model, "/v_");
  byte *data = NULL;
  int size, length;

  if (weapon_bank)
   ri.FS_FreeFile ((void *)weapon_bank);
  weapon_bank = NULL;
  weapon_frame = -1;
  strncpy (weapon_model, model, sizeof(weapon_model) - 1);
  if (name)
  {
   name++;
   length = strcspn (name, "/");
   if (length > 24)
    length = 24;
   size = WS_LoadBankFile (name, length, (void **)&data);	/* BDGN 3.58w: retro look when chosen */
   weapon_bank = WS_ValidateBank (data, size);
   if (data && !weapon_bank)
    ri.FS_FreeFile (data);
  }
 }

 h = weapon_bank;
 if (!h)
  return false;
 frame = (e->flags & RF_WEAPONSIMPLE) ? h->idle : e->frame;
 if (frame < 0 || frame >= h->frames)
  frame = 0;
 if (frame != weapon_frame || mirror != weapon_mirror)
 {
  weapon_frame = -1;
  if (!WS_DecodeFrame (h, frame, weapon_pixels))
   return false;
  if (mirror)
   for (row = 0; row < h->height; row++)
    for (col = 0; col < h->width / 2; col++)
    {
     byte *line = weapon_pixels + row * h->width;
     byte pixel = line[col];
     line[col] = line[h->width - 1 - col];
     line[h->width - 1 - col] = pixel;
    }
  weapon_frame = frame;
  weapon_mirror = mirror;
 }

 /* Match the plane used by OpenGL, including weapon pitch and roll. */
 AngleVectors (e->angles, weapon_forward, right, up);
 VectorMA (e->origin, h->depth, weapon_forward, center);
 VectorSubtract (center, r_origin, point);
 if (!(DotProduct (point, weapon_forward) > 1))
  return false;
 unit = h->depth * h->pix;
 left = mirror ? -h->u0 - h->width * h->pix : h->u0;
 for (corner = 0; corner < 4; corner++)
 {
  u = left + ((corner == 1 || corner == 2) ? h->width * h->pix : 0);
  v = h->v1 - (corner >= 2 ? h->height * h->pix : 0);
  VectorMA (center, u * h->depth, right, point);
  VectorMA (point, v * h->depth, up, weapon_corners[corner]);
  VectorSubtract (weapon_corners[corner], r_origin, point);
  if (!(DotProduct (point, vpn) > 4))
   return false;
 }
 VectorScale (right, 1.0f / unit, weapon_s_axis);
 VectorScale (up, -1.0f / unit, weapon_t_axis);
 weapon_ready = true;
 return true;
}

static void SW_WeaponSpriteDraw (void)
{
 if (weapon_ready && d_viewbuffer)
  R_DrawViewWeaponQuad (weapon_corners, weapon_forward, weapon_s_axis, weapon_t_axis,
                        weapon_pixels, weapon_bank->width, weapon_bank->height);
}
