/* Included by gl_modern_fx.h. Small, lazy light-entry mask for the current
   water surface. BSP checks keep shafts out of lava, ceilings and dry rooms. */
#define WATER_MASK (REFLECT_TEXNUM+19)
#define WATER_GRID 32
static cvar_t *modern_water_scatter;
static model_t *water_mask_model;
static int water_mask_sequence,water_mask_valid,water_mask_x,water_mask_y;
static float water_mask_height,water_mask_time;
static unsigned water_mask_light_revision;
static vec3_t water_light_dir,water_light_colour,water_light_source;
static int water_light_sun;
static unsigned char *water_wet_surfaces;
static model_t *water_wet_model;
static int water_wet_sequence;
static int ModernInWater(const vec3_t p)
{
 int contents=Mod_PointInLeaf((float *)p,r_worldmodel)->contents;
 return (contents&CONTENTS_WATER) && !(contents&(CONTENTS_LAVA|CONTENTS_SLIME|CONTENTS_SOLID));
}
static int ModernWetSurface(msurface_t *s)
{
 glpoly_t *poly; vec3_t centre,p; int i,j,index=(int)(s-r_worldmodel->surfaces),wet=0;
 float sign=(s->flags&SURF_PLANEBACK)?-1.0f:1.0f;
 if(water_wet_model!=r_worldmodel || water_wet_sequence!=registration_sequence) {
  free(water_wet_surfaces); water_wet_surfaces=NULL;
  if(r_worldmodel->numsurfaces>0 && r_worldmodel->numsurfaces<=262144)
   water_wet_surfaces=(unsigned char *)calloc(r_worldmodel->numsurfaces,1);
  water_wet_model=r_worldmodel; water_wet_sequence=registration_sequence;
 }
 if(water_wet_surfaces && water_wet_surfaces[index]) return water_wet_surfaces[index]==2;
 /* The historical SURF_UNDERWATER marking is compiled out in gl_model.c.
    Sample the visible side of the actual polygon, including mixed-height walls. */
 for(poly=s->polys;poly && !wet;poly=poly->next) {
  if(poly->numverts<3) continue;
  VectorClear(centre);
  for(i=0;i<poly->numverts;i++) VectorAdd(centre,poly->verts[i],centre);
  VectorScale(centre,1.0f/poly->numverts,centre);
  VectorMA(centre,2*sign,s->plane->normal,p); wet=ModernInWater(p);
  for(i=0;i<poly->numverts && !wet;i++) {
   for(j=0;j<3;j++) p[j]=(centre[j]+poly->verts[i][j])*.5f+2*sign*s->plane->normal[j];
   wet=ModernInWater(p);
  }
 }
 if(water_wet_surfaces) water_wet_surfaces[index]=(unsigned char)(wet?2:1);
 return wet;
}
static int ModernWaterEntry(void)
{
 vec3_t p,above,direction,colour,source; float low,high,height; int i,x,y,cx,cy,sun;
 unsigned char mask[WATER_GRID*WATER_GRID]; GLint active; glstate_t saved;
 unsigned revision=R_WaterLightRevision();
 if(!(r_newrefdef.rdflags&RDF_UNDERWATER) || !ModernInWater(r_origin)) { water_mask_valid=0; return 0; }
 VectorCopy(r_origin,p); low=p[2]; high=low;
 /* Find the actual air/water interface, rather than an arbitrary map plane. */
 for(i=0;i<256;i++) { high+=16; p[2]=high; if(!ModernInWater(p)) break; low=high; }
 if(i==256 || (Mod_PointInLeaf(p,r_worldmodel)->contents&CONTENTS_SOLID)) { water_mask_valid=0; return 0; }
 for(i=0;i<8;i++) { p[2]=(low+high)*.5f; if(ModernInWater(p)) low=p[2]; else high=p[2]; }
 height=(low+high)*.5f; p[2]=height+2;
 cx=(int)floor(r_origin[0]/128)*128; cy=(int)floor(r_origin[1]/128)*128;
 if(water_mask_valid && water_mask_model==r_worldmodel && water_mask_sequence==registration_sequence &&
    revision==water_mask_light_revision && cx==water_mask_x && cy==water_mask_y && fabs(height-water_mask_height)<.2f &&
    r_newrefdef.time>=water_mask_time && r_newrefdef.time-water_mask_time<1.0f) return 1;
 /* Keep one fixed source while swimming in this water body. Rebuilding the
    visibility mask must not select a different lamp once per second/camera cell. */
 if(!water_mask_valid || water_mask_model!=r_worldmodel || water_mask_sequence!=registration_sequence ||
    revision!=water_mask_light_revision || fabs(height-water_mask_height)>=.2f || r_newrefdef.time<water_mask_time) {
  water_mask_valid=0;
  if(!R_WaterLight(p,direction,colour,source,&sun)) return 0;
  water_light_dir[0]=direction[0]*.7501875f; water_light_dir[1]=direction[1]*.7501875f;
  water_light_dir[2]=(float)sqrt(1-water_light_dir[0]*water_light_dir[0]-water_light_dir[1]*water_light_dir[1]);
  VectorCopy(colour,water_light_colour); VectorCopy(source,water_light_source); water_light_sun=sun;
 }
 VectorCopy(water_light_source,source); sun=water_light_sun;
 for(y=0;y<WATER_GRID;y++) for(x=0;x<WATER_GRID;x++) {
  p[0]=cx-512+(x+.5f)*32; p[1]=cy-512+(y+.5f)*32; p[2]=height-2;
  VectorCopy(p,above); above[2]=height+2;
  mask[y*WATER_GRID+x]=(unsigned char)(ModernInWater(p) && !ModernInWater(above) &&
    !(Mod_PointInLeaf(above,r_worldmodel)->contents&CONTENTS_SOLID) && R_WaterLightVisible(above,source,sun)?255:0);
 }
 saved=gl_state; qglGetIntegerv(0x84E0,&active); qglPushAttrib(GL_TEXTURE_BIT);
 post_active_texture(0x84C0); qglBindTexture(GL_TEXTURE_2D,WATER_MASK);
 qglTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE,WATER_GRID,WATER_GRID,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,mask);
 qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
 qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,SCALE_CLAMP_EDGE); qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,SCALE_CLAMP_EDGE);
 qglPopAttrib(); post_active_texture(active); gl_state=saved;
 water_mask_light_revision=revision; water_mask_model=r_worldmodel; water_mask_sequence=registration_sequence; water_mask_x=cx; water_mask_y=cy; water_mask_height=height; water_mask_valid=1; water_mask_time=r_newrefdef.time;
 return 1;
}
