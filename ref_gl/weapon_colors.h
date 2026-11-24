/* Material-drawflat colours for the held stock weapons. Shared with the AMD patch builder. */
#ifndef BDGN_WEAPON_COLORS_H
#define BDGN_WEAPON_COLORS_H
static const struct {
 const char *model;
 byte rgb[3];
} bdgn_weapon_colors[] = {
 { "models/weapons/v_blast/tris.md2",  { 255, 128, 16 } },
 { "models/weapons/v_hyperb/tris.md2", { 255, 128, 16 } },
 { "models/weapons/v_rocket/tris.md2", { 255, 32, 24 } },
 { "models/weapons/v_rail/tris.md2",   { 32, 112, 255 } },
 { "models/weapons/v_bfg/tris.md2",    { 32, 240, 64 } },
 { "models/weapons/v_machn/tris.md2",  { 160, 160, 160 } },
 { "models/weapons/v_chain/tris.md2",  { 184, 184, 184 } },
 { "models/weapons/v_shotg/tris.md2",  { 72, 72, 72 } },
 { "models/weapons/v_shotg2/tris.md2", { 88, 88, 88 } },
 { "models/weapons/v_launch/tris.md2", { 176, 224, 32 } },
 { "models/weapons/v_handgr/tris.md2", { 176, 224, 32 } },
 { "models/weapons/v_grapple/tris.md2",{ 192, 80, 255 } }
};

static qboolean R_FlatWeaponColor (const char *model, byte color[3])
{
 int i;
 if (!model || !color) return false;
 for (i = 0; i < sizeof(bdgn_weapon_colors)/sizeof(bdgn_weapon_colors[0]); i++)
 {
  if (!Q_stricmp ((char *)model, (char *)bdgn_weapon_colors[i].model))
  {
   color[0] = bdgn_weapon_colors[i].rgb[0];
   color[1] = bdgn_weapon_colors[i].rgb[1];
   color[2] = bdgn_weapon_colors[i].rgb[2];
   return true;
  }
 }
 return false;
}
#endif
