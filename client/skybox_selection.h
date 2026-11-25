/* Client-only sky selection. The server's sky/configstrings are never changed. */
#ifndef Q2TTM_SKYBOX_SELECTION_H
#define Q2TTM_SKYBOX_SELECTION_H
#define Q2TTM_SKY_COUNT 10
static const char *q2ttm_sky_names[Q2TTM_SKY_COUNT] = {
 "ferrum", "crimson", "azure", "dusk", "dawn", "day", "night", "shipyard", "fleet", "eclipse"
};
static cvar_t *cl_skybox_choice;
static int cl_skybox_pack_checked, cl_skybox_pack_available;
static char cl_skybox_gamedir[MAX_OSPATH];

int CL_SkyboxAvailable(void)
{
 const char *dir;
 if (!Cvar_VariableValue("bdgn_modern_lighting_cpu") || !Cvar_VariableValue("bdgn_gl_sky_capable") ||
     Cvar_VariableValue("bdgn_legacy_graphics") || Cvar_VariableValue("fs_vanilla") ||
     Q_stricmp(Cvar_VariableString("vid_ref"), "gl")) return 0;
 dir = FS_Gamedir();
 if (!cl_skybox_pack_checked || strcmp(dir, cl_skybox_gamedir)) {
  Com_sprintf(cl_skybox_gamedir, sizeof(cl_skybox_gamedir), "%s", dir);
  cl_skybox_pack_available = FS_LoadFile("env/q2ttm_skies/catalog.txt", NULL) > 0;
  cl_skybox_pack_checked = 1;
 }
 return cl_skybox_pack_available;
}

static void CL_ApplySelectedSky(qboolean force)
{
 static const char *suffix[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
 char name[MAX_QPATH], path[MAX_QPATH];
 vec3_t axis;
 float rotate;
 int selection, i;
 if (!cl_skybox_choice) cl_skybox_choice = Cvar_Get("cl_skybox", "0", CVAR_ARCHIVE);
 if (!force && !cl_skybox_choice->modified) return;
 cl_skybox_choice->modified = false;
 if (force) cl_skybox_pack_checked = 0;
 selection = cl_skybox_choice->value >= 0 && cl_skybox_choice->value <= Q2TTM_SKY_COUNT ? (int)cl_skybox_choice->value : 0;
 if (selection < 0 || selection > Q2TTM_SKY_COUNT) selection = 0;
 Com_sprintf(name, sizeof(name), "%s", cl.configstrings[CS_SKY]);
 rotate = (float)atof(cl.configstrings[CS_SKYROTATE]);
 VectorSet(axis, 0, 0, 1);
 sscanf(cl.configstrings[CS_SKYAXIS], "%f %f %f", &axis[0], &axis[1], &axis[2]);
 if (selection && CL_SkyboxAvailable()) {
  qboolean complete = true;
  for (i = 0; i < 6; i++) {
   Com_sprintf(path, sizeof(path), "env/q2ttm_skies/%s%s.tga", q2ttm_sky_names[selection-1], suffix[i]);
   if (FS_LoadFile(path, NULL) <= 0) { complete = false; break; }
  }
  if (complete) {
   Com_sprintf(name, sizeof(name), "q2ttm_skies/%s", q2ttm_sky_names[selection-1]);
   rotate = 0;
  } else {
   Com_Printf("Skybox %s is incomplete; using the map's original sky.\n", q2ttm_sky_names[selection-1]);
  }
 }
 re.SetSky(name, rotate, axis);
}
#endif
