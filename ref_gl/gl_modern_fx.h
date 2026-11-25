/* Included by gl_render_scale.c after its GL loader. No renderer ABI changes. */
#include "gl_modern_shaders.h"
#define MODERN_COLOUR (REFLECT_TEXNUM+16)
#define MODERN_DEPTH (REFLECT_TEXNUM+17)
#define MODERN_MATERIAL (REFLECT_TEXNUM+18)
#define MODERN_BUDGET (96U*1024U*1024U)
static cvar_t *modern_pbr,*modern_soft,*modern_smoke,*modern_ssr,*modern_gi,*modern_fog,*modern_caustics;
static GLuint modern_screen,modern_puff,modern_mask_fb;
static int modern_width,modern_height,modern_stamp=-1,modern_failed;
static GLint modern_view[4];
static glstate_t modern_puff_saved;
static GLint modern_puff_active,modern_puff_program;
static int modern_puff_running;
static void (APIENTRY *modern_matrix)(GLint,GLsizei,GLboolean,const GLfloat *);
typedef struct { char name[MAX_QPATH]; int sequence; float rough,metal; } modern_material_t;
static modern_material_t *modern_materials;

typedef struct { GLuint program; GLint location; char name[32]; } modern_uniform_t;
static modern_uniform_t *modern_uniforms;
static int modern_uniform_count;
static GLint ModernUniform(GLuint program,const char *name)
{
 int i; GLint location;
 for(i=0;i<modern_uniform_count;i++) if(modern_uniforms[i].program==program && !strcmp(modern_uniforms[i].name,name)) return modern_uniforms[i].location;
 location=uniform_location(program,name);
 if(!modern_uniforms) modern_uniforms=calloc(96,sizeof(*modern_uniforms));
 if(modern_uniforms && modern_uniform_count<96) {
  i=modern_uniform_count++; modern_uniforms[i].program=program; modern_uniforms[i].location=location;
  Com_sprintf(modern_uniforms[i].name,sizeof(modern_uniforms[i].name),"%s",name);
 }
 return location;
}


static void ModernMaterial(image_t *image,float *rough,float *metal)
{
 int i; size_t address,base; char path[MAX_QPATH],text[128]; char *data=NULL,*dot; int size;
 *rough=.85f; *metal=0;
 if(!image) return;
 address=(size_t)image; base=(size_t)gltextures;
 if(address<base || address-base>=sizeof(gltextures) || (address-base)%sizeof(image_t)) return;
 i=(int)((address-base)/sizeof(image_t));
 if(!modern_materials) modern_materials=calloc(MAX_GLTEXTURES,sizeof(*modern_materials));
 if(!modern_materials) return;
 if(modern_materials[i].sequence==image->registration_sequence &&
   !strcmp(modern_materials[i].name,image->name)) {
  *rough=modern_materials[i].rough; *metal=modern_materials[i].metal; return;
 }
 /* Conservative fallback; explicit sidecars override these name hints. */
 if(strstr(image->name,"metal") || strstr(image->name,"pipe") || strstr(image->name,"grate")) { *rough=.45f; *metal=.75f; }
 if(!strncmp(image->name,"textures/",9)) {
  Com_sprintf(path,sizeof(path),"materials/%s",image->name+9);
  dot=strrchr(path,'.'); if(dot && dot-path<MAX_QPATH-5) strcpy(dot,".pbr");
  size=ri.FS_LoadFile(path,(void **)&data);
  if(data) {
   if(size>0 && size<(int)sizeof(text)) {
    memcpy(text,data,size); text[size]=0;
    if(sscanf(text,"%f %f",rough,metal)!=2) { *rough=.85f; *metal=0; }
   }
   ri.FS_FreeFile(data);
  }
 }
 if(!(*rough>=.08f && *rough<=1)) *rough=.85f;
 if(!(*metal>=0 && *metal<=1)) *metal=0;
 Com_sprintf(modern_materials[i].name,sizeof(modern_materials[i].name),"%s",image->name);
 modern_materials[i].sequence=image->registration_sequence;
 modern_materials[i].rough=*rough; modern_materials[i].metal=*metal;
}

static void ModernShutdown(void)
{
 GLuint textures[3]={MODERN_COLOUR,MODERN_DEPTH,MODERN_MATERIAL};
 if(modern_screen) delete_program(modern_screen);
 if(modern_puff) delete_program(modern_puff);
 if(modern_mask_fb && del_fb) del_fb(1,&modern_mask_fb);
 if(modern_width && qglDeleteTextures) qglDeleteTextures(3,textures);
 free(modern_uniforms); modern_uniforms=NULL; modern_uniform_count=0;
 free(modern_materials); modern_materials=NULL;
 modern_screen=modern_puff=modern_mask_fb=0;
 modern_width=modern_height=modern_failed=modern_puff_running=0; modern_stamp=-1;
}

void GL_InitModernEffects(void)
{
 modern_pbr=ri.Cvar_Get("gl_materials","0",CVAR_ARCHIVE);
 modern_soft=ri.Cvar_Get("gl_soft_particles","0",CVAR_ARCHIVE);
 modern_smoke=ri.Cvar_Get("gl_smoke_lighting","0",CVAR_ARCHIVE);
 modern_ssr=ri.Cvar_Get("gl_ssr","0",CVAR_ARCHIVE);
 modern_gi=ri.Cvar_Get("gl_ssgi","0",CVAR_ARCHIVE);
 modern_fog=ri.Cvar_Get("gl_volume_fog","0",CVAR_ARCHIVE);
 modern_caustics=ri.Cvar_Get("gl_caustics","0",CVAR_ARCHIVE);
 ri.Cvar_SetValue("bdgn_gl_materials_capable",0);
 ri.Cvar_SetValue("bdgn_gl_soft_particles_capable",0);
 ri.Cvar_SetValue("bdgn_gl_environment_capable",0);
#ifdef _WIN32
 /* No proc lookup, compile or allocation until both the CPU and driver passed. */
 if(!gl_soft_shadow_capable || !pixel_light_program || !(post_available&POST_SSAO) ||
    !qglCopyTexSubImage2D || !qglPushAttrib || !qglPopAttrib) return;
 SCALE_PROC(modern_matrix,"glUniformMatrix4fv",NULL);
 if(!modern_matrix || !pixel_uniform4f || !pixel_uniform1f || !post_uniform1i || !post_active_texture) return;
 modern_puff=ScaleCompileShader(modern_puff_shader,modern_vertex);
 modern_screen=ScaleCompileShader(modern_screen_shader,NULL);
 if(modern_puff || modern_screen) { post_available|=POST_MODERN; ri.Cvar_SetValue("bdgn_gl_post_available",post_available); }
 ri.Cvar_SetValue("bdgn_gl_materials_capable",pixel_pbr>=0 && pixel_material>=0);
 ri.Cvar_SetValue("bdgn_gl_soft_particles_capable",modern_puff!=0);
 ri.Cvar_SetValue("bdgn_gl_environment_capable",modern_screen!=0);
#endif
}

static int ModernWorld(void)
{
 return !modern_failed && !gl_reflect_pass && !gl_ztrick->value && r_worldmodel &&
  !(r_newrefdef.rdflags&RDF_NOWORLDMODEL) && r_drawworld->value && !GL_FULLBRIGHT() &&
  !gl_hack_noimages->value && !gl_nobind->value;
}

int GL_ModernNeedsCleanWorld(void)
{
 if(!modern_screen && !modern_puff) return 0;
 return ModernWorld() && ((modern_puff && (modern_soft->value || modern_smoke->value)) ||
  (modern_screen && (modern_ssr->value || modern_gi->value || modern_fog->value ||
    (modern_caustics->value && (r_newrefdef.rdflags&RDF_UNDERWATER)))));
}

static void ModernTexture(GLuint texture,GLint format,int depth)
{
 qglBindTexture(GL_TEXTURE_2D,texture);
 qglTexImage2D(GL_TEXTURE_2D,0,format,modern_width,modern_height,0,depth?GL_DEPTH_COMPONENT:GL_RGBA,depth?GL_UNSIGNED_INT:GL_UNSIGNED_BYTE,NULL);
 qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,depth?GL_NEAREST:GL_LINEAR);
 qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,depth?GL_NEAREST:GL_LINEAR);
 qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,SCALE_CLAMP_EDGE);
 qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,SCALE_CLAMP_EDGE);
}

void GL_ModernOpaqueDepth(void)
{
 GLint limit,w=1,h=1,previous; GLenum error; glstate_t saved;
 modern_stamp=-1;
 if(!GL_ModernNeedsCleanWorld()) return;
 qglGetIntegerv(GL_VIEWPORT,modern_view);
 if(modern_view[2]<16 || modern_view[3]<16) return;
 w=modern_view[2]; h=modern_view[3]; /* GL2 core NPOT: no padded 4K allocations. */
 qglGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
 if(w>limit || h>limit || (unsigned)w*(unsigned)h>MODERN_BUDGET/12) return;
 ScaleClearErrors();
 saved=gl_state; qglPushAttrib(GL_TEXTURE_BIT); post_active_texture(0x84C0);
 if(w!=modern_width || h!=modern_height) {
  modern_width=w; modern_height=h;
  ModernTexture(MODERN_DEPTH,0x81A6,1);
  ModernTexture(MODERN_COLOUR,GL_RGBA8,0);
  ModernTexture(MODERN_MATERIAL,GL_RGBA8,0);
 }
 qglBindTexture(GL_TEXTURE_2D,MODERN_DEPTH);
 qglCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,modern_view[0],modern_view[1],modern_view[2],modern_view[3]);
 error=qglGetError();
 if(error==GL_NO_ERROR && !modern_mask_fb) {
  qglGetIntegerv(0x8CA6,&previous); gen_fb(1,&modern_mask_fb); bind_fb(SCALE_FB,modern_mask_fb);
  attach_tex(SCALE_FB,SCALE_COLOR,GL_TEXTURE_2D,MODERN_MATERIAL,0);
  attach_tex(SCALE_FB,SCALE_DEPTH,GL_TEXTURE_2D,MODERN_DEPTH,0);
  if(check_fb(SCALE_FB)!=SCALE_COMPLETE) error=GL_INVALID_OPERATION;
  bind_fb(SCALE_FB,previous);
 }
 post_active_texture(0x84C0); qglPopAttrib(); gl_state=saved;
 if(error==GL_NO_ERROR) modern_stamp=r_framecount;
 else { modern_failed=1; ri.Con_Printf(PRINT_ALL,"Modern environment buffer unavailable; original rendering retained.\n"); }
}

static void ModernLights(GLuint program,int coloured)
{
 int i,j,count=0,selected[4]; float scores[4];
 for(i=0;coloured && gl_dynamic->value && i<r_newrefdef.num_dlights;i++) {
  const dlight_t *l=&r_newrefdef.dlights[i]; vec3_t delta; float score;
  if(!(l->intensity>0) || l->color[0]<0 || l->color[1]<0 || l->color[2]<0) continue;
  VectorSubtract(l->origin,r_origin,delta); score=l->intensity/(64+VectorLength(delta));
  for(j=count;j>0 && scores[j-1]<score;j--) if(j<4) { scores[j]=scores[j-1]; selected[j]=selected[j-1]; }
  if(j<4) { scores[j]=score; selected[j]=i; if(count<4) count++; }
 }
 for(i=0;i<4;i++) {
  char name[32]; GLint loc;
  Com_sprintf(name,sizeof(name),"lightPos[%d]",i); loc=ModernUniform(program,name);
  if(i<count) {
   const dlight_t *l=&r_newrefdef.dlights[selected[i]];
   pixel_uniform4f(loc,l->origin[0],l->origin[1],l->origin[2],1/l->intensity);
   Com_sprintf(name,sizeof(name),"lightCol[%d]",i); loc=ModernUniform(program,name);
   pixel_uniform4f(loc,l->color[0],l->color[1],l->color[2],0);
  } else {
   pixel_uniform4f(loc,0,0,0,1);
   Com_sprintf(name,sizeof(name),"lightCol[%d]",i); pixel_uniform4f(ModernUniform(program,name),0,0,0,0);
  }
 }
}

int GL_ModernSmokeColour(void) { return modern_puff && modern_smoke->value && ModernWorld(); }
void GL_ModernPuffBegin(int premultiplied)
{
 modern_puff_running=0;
 if(modern_stamp!=r_framecount || !modern_puff || (!modern_soft->value && !modern_smoke->value)) return;
 modern_puff_saved=gl_state; qglGetIntegerv(0x84E0,&modern_puff_active); qglGetIntegerv(0x8B8D,&modern_puff_program);
 qglPushAttrib(GL_TEXTURE_BIT);
 post_active_texture(0x84C1); qglBindTexture(GL_TEXTURE_2D,MODERN_DEPTH); post_active_texture(0x84C0);
 use_program(modern_puff);
 post_uniform1i(ModernUniform(modern_puff,"puffTex"),0); post_uniform1i(ModernUniform(modern_puff,"opaqueDepth"),1);
 pixel_uniform4f(ModernUniform(modern_puff,"viewport"),(float)modern_view[0],(float)modern_view[1],(float)modern_view[2],(float)modern_view[3]);
 uniform2f(ModernUniform(modern_puff,"textureSize"),(float)modern_width,(float)modern_height);
 uniform2f(ModernUniform(modern_puff,"range"),gldepthmin,1/(gldepthmax-gldepthmin));
 uniform2f(ModernUniform(modern_puff,"options"),(float)premultiplied,r_drawdistance>0?r_drawdistance+32:4096);
 pixel_uniform1f(ModernUniform(modern_puff,"softEnabled"),modern_soft->value?1.0f:0.0f);
 ModernLights(modern_puff,modern_smoke->value!=0); modern_puff_running=1;
}
void GL_ModernPuffEnd(void)
{
 if(!modern_puff_running) return;
 use_program(modern_puff_program); qglPopAttrib(); post_active_texture(modern_puff_active);
 gl_state=modern_puff_saved; modern_puff_running=0;
}

static int ModernInverse(const GLfloat *m,GLfloat *inverse)
{
 double a[4][8]; int i,j,k,pivot; double scale;
 for(i=0;i<4;i++) for(j=0;j<8;j++) a[i][j]=j<4?m[j*4+i]:(j-4==i?1:0);
 for(i=0;i<4;i++) {
  pivot=i; for(j=i+1;j<4;j++) if(fabs(a[j][i])>fabs(a[pivot][i])) pivot=j;
  if(fabs(a[pivot][i])<1e-10) return 0;
  if(pivot!=i) for(k=0;k<8;k++) { double tmp=a[i][k]; a[i][k]=a[pivot][k]; a[pivot][k]=tmp; }
  scale=a[i][i]; for(k=0;k<8;k++) a[i][k]/=scale;
  for(j=0;j<4;j++) if(j!=i) { scale=a[j][i]; for(k=0;k<8;k++) a[j][k]-=scale*a[i][k]; }
 }
 for(i=0;i<4;i++) for(j=0;j<4;j++) inverse[j*4+i]=(float)a[i][j+4];
 return 1;
}

static void ModernMask(void)
{
 msurface_t *s; int i,j; GLfloat rough,metal;
 bind_fb(SCALE_FB,modern_mask_fb); qglViewport(0,0,modern_view[2],modern_view[3]);
 qglClearColor(1,0,0,0); qglColorMask(1,1,1,1); qglDisable(GL_SCISSOR_TEST); qglClear(GL_COLOR_BUFFER_BIT);
 use_program(0); GL_EnableMultitexture(false); qglDisable(GL_TEXTURE_2D); qglDisable(GL_BLEND); qglDisable(GL_ALPHA_TEST);
 qglEnable(GL_DEPTH_TEST); qglDepthFunc(GL_LEQUAL); qglDepthMask(GL_FALSE);
 qglEnable(GL_POLYGON_OFFSET_FILL); qglPolygonOffset(-1,-2);
 qglMatrixMode(GL_MODELVIEW); qglLoadMatrixf(r_world_matrix);
 for(i=0,s=r_worldmodel->surfaces;i<r_worldmodel->numsurfaces;i++,s++) {
  glpoly_t *p=s->polys; float side;
  if(s->visframe!=r_framecount || !p || (s->texinfo->flags&(SURF_SKY|SURF_WARP|SURF_TRANS33|SURF_TRANS66))) continue;
  side=DotProduct(r_origin,s->plane->normal)-s->plane->dist;
  if((side<0)!=((s->flags&SURF_PLANEBACK)!=0)) continue;
  ModernMaterial(R_TextureAnimationFrame(s->texinfo,(int)(r_map_animation_time*2)),&rough,&metal);
  if((1-rough)*(.04f+.55f*metal)<=.015f) continue;
  qglColor4f(rough,metal,0,1); qglBegin(GL_POLYGON);
  for(j=0;j<p->numverts;j++) qglVertex3fv(p->verts[j]); qglEnd();
 }
}

void GL_ModernEnvironment(void)
{
 GLfloat projection[16],inverse[16]; GLint previous,program,mode,active; glstate_t saved; int i;
 if(modern_stamp!=r_framecount || !modern_screen || !ModernWorld() ||
   (!modern_ssr->value && !modern_gi->value && !modern_fog->value &&
    !(modern_caustics->value && (r_newrefdef.rdflags&RDF_UNDERWATER)))) return;
 qglGetFloatv(GL_PROJECTION_MATRIX,projection); if(!ModernInverse(projection,inverse)) return;
 saved=gl_state; qglGetIntegerv(0x8CA6,&previous); qglGetIntegerv(0x8B8D,&program); qglGetIntegerv(GL_MATRIX_MODE,&mode); qglGetIntegerv(0x84E0,&active);
 qglPushAttrib(GL_ALL_ATTRIB_BITS); qglMatrixMode(GL_MODELVIEW); qglPushMatrix(); qglMatrixMode(GL_PROJECTION); qglPushMatrix();
 if(modern_ssr->value) ModernMask();
 bind_fb(SCALE_FB,previous); qglViewport(modern_view[0],modern_view[1],modern_view[2],modern_view[3]);
 post_active_texture(0x84C0); qglBindTexture(GL_TEXTURE_2D,MODERN_COLOUR);
 qglCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,modern_view[0],modern_view[1],modern_view[2],modern_view[3]);
 for(i=0;i<3;i++) { post_active_texture(0x84C0+i); qglBindTexture(GL_TEXTURE_2D,i==0?MODERN_COLOUR:i==1?MODERN_DEPTH:MODERN_MATERIAL); }
 post_active_texture(0x84C0); qglEnable(GL_TEXTURE_2D); qglDisable(GL_DEPTH_TEST); qglDisable(GL_BLEND); qglDisable(GL_ALPHA_TEST); qglDisable(GL_CULL_FACE); qglDepthMask(0);
 qglColorMask(1,1,1,1); qglDisable(GL_SCISSOR_TEST); qglMatrixMode(GL_PROJECTION); qglLoadIdentity(); qglMatrixMode(GL_MODELVIEW); qglLoadIdentity();
 use_program(modern_screen); post_uniform1i(ModernUniform(modern_screen,"scene"),0); post_uniform1i(ModernUniform(modern_screen,"depthTex"),1); post_uniform1i(ModernUniform(modern_screen,"materialTex"),2);
 pixel_uniform4f(ModernUniform(modern_screen,"viewport"),(float)modern_view[0],(float)modern_view[1],(float)modern_view[2],(float)modern_view[3]);
 uniform2f(ModernUniform(modern_screen,"textureSize"),(float)modern_width,(float)modern_height);
 uniform2f(ModernUniform(modern_screen,"range"),gldepthmin,1/(gldepthmax-gldepthmin));
 modern_matrix(ModernUniform(modern_screen,"inverseProjection"),1,GL_FALSE,inverse); modern_matrix(ModernUniform(modern_screen,"projection"),1,GL_FALSE,projection);
 pixel_uniform4f(ModernUniform(modern_screen,"modes"),modern_ssr->value>=2?2:modern_ssr->value>0?1:0,modern_gi->value>=2?2:modern_gi->value>0?1:0,modern_fog->value>=2?2:modern_fog->value>0?1:0,modern_caustics->value && (r_newrefdef.rdflags&RDF_UNDERWATER)?1:0);
 pixel_uniform4f(ModernUniform(modern_screen,"params"),fmod(r_newrefdef.time,4096.0),0,0,0);
 pixel_uniform4f(ModernUniform(modern_screen,"eyeOrigin"),r_origin[0],r_origin[1],r_origin[2],0);
 pixel_uniform4f(ModernUniform(modern_screen,"rightAxis"),vright[0],vright[1],vright[2],0);
 pixel_uniform4f(ModernUniform(modern_screen,"upAxis"),vup[0],vup[1],vup[2],0);
 pixel_uniform4f(ModernUniform(modern_screen,"forwardAxis"),vpn[0],vpn[1],vpn[2],0);
 ModernLights(modern_screen,modern_fog->value!=0);
 qglBegin(GL_QUADS); qglVertex2f(-1,-1); qglVertex2f(1,-1); qglVertex2f(1,1); qglVertex2f(-1,1); qglEnd();
 use_program(program); qglMatrixMode(GL_PROJECTION); qglPopMatrix(); qglMatrixMode(GL_MODELVIEW); qglPopMatrix(); qglMatrixMode(mode);
 qglPopAttrib(); post_active_texture(active); gl_state=saved;
}
