#ifndef BDGN_MAP_MEMORY_H
#define BDGN_MAP_MEMORY_H

/* Included by the software and OpenGL model loaders. This is a virtual
 * address reservation. The hunk commits only the pages it uses. */
static int BDGN_MapHunkSize(void)
{
 int ram=(int)ri.Cvar_Get("bdgn_detected_ram_mb","0",CVAR_NOSET)->value;
 float mb=ri.Cvar_Get("r_map_hunk_mb","0",CVAR_ARCHIVE)->value;

 if (ram<256 || !ri.Cvar_Get("bdgn_nt_memory","0",CVAR_NOSET)->value || ri.Cvar_Get("bdgn_legacy_graphics","0",CVAR_NOSET)->value ||
     ri.Cvar_Get("bdgn_lowram","0",0)->value)
  return 16*1024*1024;
 if (!(mb>=16))
  mb=64;
 if (mb>128)
  mb=128;
 return (int)mb*1024*1024;
}

#endif
