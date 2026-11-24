/* Legacy OpenGL render scale. The 3D view is copied before drawing the
   enlarged view and the native-resolution HUD into the same back buffer. */
#include "gl_local.h"
#include "../client/render_scale.h"
#include "gl_post_shader.h"

#define SCALE_TILE_LIMIT 64
#define SCALE_MEMORY_LIMIT (48U*1024U*1024U)
#define POST_MEMORY_LIMIT (80U*1024U*1024U)
#define SCALE_TEX_BASE 640	/* BDGN 3.59k: below the 2D weapon/enemy names (768-927, gl_local.h), not above every image (5505+): 3dfx MiniGL */
enum { SCALE_NATIVE, SCALE_COPY, SCALE_READBACK, SCALE_FBO, SCALE_PIXEL_COPY };
static cvar_t *post_fxaa,*post_bloom,*post_ssao,*post_tonemap,*post_grade,*post_brightness;
static int post_available,post_frame,post_reported,post_target_request,post_rejected_request;
static GLuint post_program,post_depth_texture,pixel_light_program;
static cvar_t *pixel_lighting,*pixel_bump,*pixel_models;
/* BDGN 3.55h: up to PIXEL_MAX_LIGHTS dynamic lights per pixel, with surface normals, on world surfaces and models */
#define PIXEL_MAX_LIGHTS 4
static GLint pixel_pos_loc[PIXEL_MAX_LIGHTS],pixel_col_loc[PIXEL_MAX_LIGHTS],pixel_sampler,pixel_texel,pixel_bump_loc;
static GLint pixel_height_lod,pixel_source_texel;
static void (APIENTRY *pixel_uniform4f)(GLint,GLfloat,GLfloat,GLfloat,GLfloat);
static void (APIENTRY *pixel_uniform1f)(GLint,GLfloat);
static void (APIENTRY *pixel_texcoord3)(GLenum,GLfloat,GLfloat,GLfloat);
int gl_pixel_lightmask;		/* lights drawn per pixel this frame (bit per r_newrefdef.dlights index) */
int gl_pixel_model_skip;	/* set while lighting a model that gets the per-pixel pass (gl_light.c R_LightPoint) */
static int pixel_count,pixel_index[PIXEL_MAX_LIGHTS],pixel_eye_frame=-1;
static float pixel_eye[PIXEL_MAX_LIGHTS][4];
/* BDGN 3.55o: planar water reflection (step 3 of the lighting plan) */
static GLuint reflect_program,reflect_fbo,reflect_rb;
static GLint reflect_loc_tex,reflect_loc_view,reflect_loc_eye,reflect_loc_time,reflect_loc_strength;
static void (APIENTRY *reflect_uniform3f)(GLint,GLfloat,GLfloat,GLfloat);
static int reflect_w,reflect_h,reflect_ready,reflect_failed,reflect_next_valid;
static GLint reflect_prev_fb,reflect_prev_view[4];
static float reflect_height,reflect_next_height,reflect_next_dist;
static cvar_t *water_reflect;
static cvar_t *water_surface, *water_interaction;
static GLuint water_program;
static int water_drawn_surfaces;
static const entity_t *water_view_ripples[MAX_WATER_RIPPLES];
static int water_view_ripple_count=-1; /* -1 until the first interactive water surface */
static int weapon_haze_beams, haze_copy_failed;
static GLint water_tex_loc, water_view_loc, water_ripple_loc[4];
qboolean gl_reflect_pass;
#define REFLECT_TEXNUM (SCALE_TEX_BASE+SCALE_TILE_LIMIT+8)
static GLint post_step,post_end,post_effects,post_ao_sharpen,post_depth_range,post_ao_params,post_sampler,post_colour,post_brightness_loc;
static void (APIENTRY *post_active_texture)(GLenum);
static void (APIENTRY *post_uniform1i)(GLint,GLint);
typedef struct {
 int x,y,w,h;
 float u,v;
 GLuint texture;
} gl_scale_tile_t;

static cvar_t *scale_value,*scale_filter,*scale_legacy,*scale_pixelcopy,*scale_copyformat;
#define SCALE_FB 0x8D40
#define SCALE_RB 0x8D41
#define SCALE_COLOR 0x8CE0
#define SCALE_DEPTH 0x8D00
#define SCALE_COMPLETE 0x8CD5
#define SCALE_DEPTH16 0x81A5
#define SCALE_DEPTH24 0x81A6
#define SCALE_CLAMP_EDGE 0x812F
static void (APIENTRY *gen_fb)(GLsizei,GLuint *);
static void (APIENTRY *del_fb)(GLsizei,const GLuint *);
static void (APIENTRY *bind_fb)(GLenum,GLuint);
static void (APIENTRY *attach_tex)(GLenum,GLenum,GLenum,GLuint,GLint);
static GLenum (APIENTRY *check_fb)(GLenum);
static void (APIENTRY *gen_rb)(GLsizei,GLuint *);
static void (APIENTRY *del_rb)(GLsizei,const GLuint *);
static void (APIENTRY *bind_rb)(GLenum,GLuint);
static void (APIENTRY *alloc_rb)(GLenum,GLenum,GLsizei,GLsizei);
static void (APIENTRY *attach_rb)(GLenum,GLenum,GLenum,GLuint);
static GLuint scale_fb,scale_depth,scale_program;
static qboolean scale_offscreen,scale_shader_checked,scale_fbo_available,scale_legacy_graphics;
static int rejected_width,rejected_height,reported_filter,texture_filter;
static GLint scale_shader_step,scale_shader_end;
static GLuint (APIENTRY *create_shader)(GLenum);
static void (APIENTRY *shader_source)(GLuint,GLsizei,const char **,const GLint *);
static void (APIENTRY *compile_shader)(GLuint);
static void (APIENTRY *get_shader)(GLuint,GLenum,GLint *);
static void (APIENTRY *delete_shader)(GLuint);
static GLuint (APIENTRY *create_program)(void);
static void (APIENTRY *attach_shader)(GLuint,GLuint);
static void (APIENTRY *link_program)(GLuint);
static void (APIENTRY *get_program)(GLuint,GLenum,GLint *);
static void (APIENTRY *delete_program)(GLuint);
static void (APIENTRY *use_program)(GLuint);
static GLint (APIENTRY *uniform_location)(GLuint,const char *);
static void (APIENTRY *uniform2f)(GLint,GLfloat,GLfloat);

#ifdef _WIN32
static PROC ScaleProc(const char *name,const char *fallback)
{
 PROC proc;
 if (!Q_stricmp(gl_driver->string,"mtgl32") || !Q_stricmp(gl_driver->string,"mtgl32.dll"))
  return NULL; /* the wrapper owns the GL context on its worker */
 proc=qwglGetProcAddress?qwglGetProcAddress(name):NULL;
 if(((size_t)proc<=3 || proc==(PROC)-1) && fallback && qwglGetProcAddress)
  proc=qwglGetProcAddress(fallback);
 return (size_t)proc<=3 || proc==(PROC)-1 ? NULL : proc;
}
#define SCALE_PROC(field,name,ext) do { \
 PROC proc=ScaleProc(name,ext); \
 memcpy(&(field),&proc,sizeof(field)); \
} while(0)

/* Local tokens keep the probe buildable with the OpenGL 1.1 Win32 headers. */
#define SCALE_WGL_ACCELERATION 0x2003
#define SCALE_WGL_SUPPORT_OPENGL 0x2010
#define SCALE_WGL_DEPTH_BITS 0x2022
#define SCALE_WGL_FULL_ACCELERATION 0x2027
#define SCALE_WGL_DRAW_TO_PBUFFER 0x202D
#define SCALE_WGL_BIND_RGB 0x2070
#define SCALE_WGL_BIND_RGBA 0x2071

static qboolean ScaleHasExtension(const char *extensions,const char *name)
{
 const char *match=extensions;
 size_t len=strlen(name);
 while((match=strstr(match,name))!=NULL) {
  if((match==extensions || match[-1]==' ') && (match[len]==' ' || !match[len]))
   return true;
  match+=len;
 }
 return false;
}

static void ScaleCheckWGL(void)
{
 static const char *required_procs[]={
  "wglCreatePbufferARB","wglGetPbufferDCARB","wglReleasePbufferDCARB",
  "wglDestroyPbufferARB","wglQueryPbufferARB","wglBindTexImageARB",
  "wglReleaseTexImageARB",NULL
 };
 const char *(WINAPI *get_arb)(HDC);
 const char *(WINAPI *get_ext)(void);
 BOOL (WINAPI *choose_format)(HDC,const int *,const FLOAT *,UINT,int *,UINT *);
 const char *extensions=NULL;
 HDC dc=NULL;
 qboolean pixel_format,pbuffer,render_texture;
 int attrs[]={
  SCALE_WGL_BIND_RGB,TRUE,
  SCALE_WGL_DRAW_TO_PBUFFER,TRUE,
  SCALE_WGL_SUPPORT_OPENGL,TRUE,
  SCALE_WGL_ACCELERATION,SCALE_WGL_FULL_ACCELERATION,
  SCALE_WGL_DEPTH_BITS,16,0
 };
 int i;

 SCALE_PROC(get_arb,"wglGetExtensionsStringARB",NULL);
 SCALE_PROC(get_ext,"wglGetExtensionsStringEXT",NULL);
 if((get_arb || get_ext) && qwglGetCurrentDC)
  dc=qwglGetCurrentDC();
 if(dc && get_arb)
  extensions=get_arb(dc);
 if(!extensions && get_ext)
  extensions=get_ext();
 if(!extensions) {
  ri.Con_Printf(PRINT_ALL,"WGL extensions: unavailable for this context or wrapper.\n");
  return;
 }

 pixel_format=ScaleHasExtension(extensions,"WGL_ARB_pixel_format");
 pbuffer=ScaleHasExtension(extensions,"WGL_ARB_pbuffer");
 render_texture=ScaleHasExtension(extensions,"WGL_ARB_render_texture");
 ri.Con_Printf(PRINT_ALL,"WGL_ARB_pixel_format %s, WGL_ARB_pbuffer %s (EXT %s)\n",
  pixel_format?"yes":"no",pbuffer?"yes":"no",
  ScaleHasExtension(extensions,"WGL_EXT_pbuffer")?"yes":"no");
 ri.Con_Printf(PRINT_ALL,"WGL_ARB_render_texture %s\n",render_texture?"yes":"no");
 if(!pixel_format || !pbuffer || !render_texture) {
  ri.Con_Printf(PRINT_ALL,"WGL render to texture: required extensions missing.\n");
  return;
 }

 SCALE_PROC(choose_format,"wglChoosePixelFormatARB",NULL);
 if(!dc || !choose_format || !qwglShareLists) {
  ri.Con_Printf(PRINT_ALL,"WGL render to texture: current DC or required functions unavailable.\n");
  return;
 }
 for(i=0;required_procs[i];i++) {
  if(!ScaleProc(required_procs[i],NULL)) {
   ri.Con_Printf(PRINT_ALL,"WGL render to texture: missing %s.\n",required_procs[i]);
   return;
  }
 }
 for(i=0;i<2;i++) {
  int format=0;
  UINT count=0;
  attrs[0]=i?SCALE_WGL_BIND_RGBA:SCALE_WGL_BIND_RGB;
  if(choose_format(dc,attrs,NULL,1,&format,&count) && count && format>0) {
   ri.Con_Printf(PRINT_ALL,"WGL render to texture: accelerated %s pbuffer format %d found.\n",
    i?"RGBA":"RGB",format);
   return;
  }
 }
 ri.Con_Printf(PRINT_ALL,"WGL render to texture: no accelerated RGB/RGBA pbuffer format with depth.\n");
}
#endif

static void ScaleLoadFBO(void)
{
#ifdef _WIN32
 const char *extensions=gl_config.extensions_string;
#endif
 scale_fbo_available=false;
#ifdef _WIN32
 if ((!gl_config.version_string || gl_config.version_string[0]<'3') &&
     (!extensions || (!strstr(extensions,"GL_EXT_framebuffer_object") &&
                      !strstr(extensions,"GL_ARB_framebuffer_object"))))
  return;
 SCALE_PROC(gen_fb,"glGenFramebuffers","glGenFramebuffersEXT");
 SCALE_PROC(del_fb,"glDeleteFramebuffers","glDeleteFramebuffersEXT");
 SCALE_PROC(bind_fb,"glBindFramebuffer","glBindFramebufferEXT");
 SCALE_PROC(attach_tex,"glFramebufferTexture2D","glFramebufferTexture2DEXT");
 SCALE_PROC(check_fb,"glCheckFramebufferStatus","glCheckFramebufferStatusEXT");
 SCALE_PROC(gen_rb,"glGenRenderbuffers","glGenRenderbuffersEXT");
 SCALE_PROC(del_rb,"glDeleteRenderbuffers","glDeleteRenderbuffersEXT");
 SCALE_PROC(bind_rb,"glBindRenderbuffer","glBindRenderbufferEXT");
 SCALE_PROC(alloc_rb,"glRenderbufferStorage","glRenderbufferStorageEXT");
 SCALE_PROC(attach_rb,"glFramebufferRenderbuffer","glFramebufferRenderbufferEXT");
 scale_fbo_available=gen_fb && del_fb && bind_fb && attach_tex && check_fb &&
  gen_rb && del_rb && bind_rb && alloc_rb && attach_rb && qglGetIntegerv;
#endif
}

static GLuint ScaleCompileShader(const char *source,const char *vertex);
static void GL_InitModernWater(void);

static void ScaleLoadShader(void)
{
 static const char *source=
  "#version 110\n"
  "uniform sampler2D image; uniform vec2 stepUV,endUV;"
  "vec3 tap(vec2 p){return texture2D(image,clamp(p,stepUV*.5,endUV-stepUV*.5)).rgb;}"
  "float lum(vec3 v){return dot(v,vec3(.299,.587,.114));}"
  "void main(){vec2 p=gl_TexCoord[0].st; vec3 c=tap(p);"
  "vec3 l=tap(p-vec2(stepUV.x,0.)), r=tap(p+vec2(stepUV.x,0.));"
  "vec3 u=tap(p+vec2(0.,stepUV.y)), d=tap(p-vec2(0.,stepUV.y));"
  "float gx=abs(lum(l)-lum(r)), gy=abs(lum(u)-lum(d));"
  "float axis=clamp(.5+(gx-gy)*2.,0.,1.);"
  "vec3 smoothColor=mix((l+r)*.5,(u+d)*.5,axis);"
  "gl_FragColor=vec4(clamp(c+.28*(c-smoothColor),min(c,min(min(l,r),min(u,d))),"
  "max(c,max(max(l,r),max(u,d)))),1.);}";
 scale_program=ScaleCompileShader(source,NULL);
 if(scale_program) {
  scale_shader_step=uniform_location(scale_program,"stepUV");
  scale_shader_end=uniform_location(scale_program,"endUV");
 }
}

static GLuint ScaleCompileShader(const char *source,const char *vertex)
{
#ifdef _WIN32
 GLuint shader,vs=0,program=0;
 GLint ok=0;
 SCALE_PROC(create_shader,"glCreateShader",NULL);
 SCALE_PROC(shader_source,"glShaderSource",NULL);
 SCALE_PROC(compile_shader,"glCompileShader",NULL);
 SCALE_PROC(get_shader,"glGetShaderiv",NULL);
 SCALE_PROC(delete_shader,"glDeleteShader",NULL);
 SCALE_PROC(create_program,"glCreateProgram",NULL);
 SCALE_PROC(attach_shader,"glAttachShader",NULL);
 SCALE_PROC(link_program,"glLinkProgram",NULL);
 SCALE_PROC(get_program,"glGetProgramiv",NULL);
 SCALE_PROC(delete_program,"glDeleteProgram",NULL);
 SCALE_PROC(use_program,"glUseProgram",NULL);
 SCALE_PROC(uniform_location,"glGetUniformLocation",NULL);
 SCALE_PROC(uniform2f,"glUniform2f",NULL);
 if(!create_shader||!shader_source||!compile_shader||!get_shader||!delete_shader||
    !create_program||!attach_shader||!link_program||!get_program||!delete_program||
    !use_program||!uniform_location||!uniform2f) return 0;
 shader=create_shader(0x8B30);
 if(!shader) return 0;
 shader_source(shader,1,&source,NULL);
 compile_shader(shader);
 get_shader(shader,0x8B81,&ok);
 if(ok && vertex) {
  vs=create_shader(0x8B31);
  if(vs) {
   shader_source(vs,1,&vertex,NULL);
   compile_shader(vs);
   get_shader(vs,0x8B81,&ok);
  } else ok=0;
 }
 if(ok) {
  program=create_program();
  if(program) {
   attach_shader(program,shader);
   if(vs) attach_shader(program,vs);
   link_program(program);
   get_program(program,0x8B82,&ok);
   if(!ok) { delete_program(program); program=0; }
  }
 }
 delete_shader(shader);
 if(vs) delete_shader(vs);
 return program;
#else
 (void)source;
 (void)vertex;
 return 0;
#endif
}

static void GL_InitRelief(void);
static void GL_InitGodRays(void);
void GL_InitPixelLighting(void)
{
#ifdef _WIN32
 /* BDGN 3.55h: eye-space lighting for world surfaces and alias models. Each light: smooth radial falloff (as the
    v3.55e pass), facing test against the geometric normal and Lambert term on the (optionally bumped) normal.
    The relief is taken from the texture's own brightness in the texture's s/t directions. */
 static const char *source=
  "#version 110\n"
  "uniform vec4 lightPos[4],lightCol[4]; uniform sampler2D baseTexture; uniform vec2 texel,sourceTexel; uniform float bump,heightLod;\n"
  "varying vec3 P,N,S,T;\n"
  "float lum(vec3 c){return dot(c,vec3(0.299,0.587,0.114));}\n"
  /* Colour noise in an HD replacement is not extra geometry. Filter height
     at the original material density, while colour keeps its normal LOD. */
  "float heightAt(vec2 uv,float bias){return lum(texture2D(baseTexture,uv,bias).rgb);}\n"
  "void main(){vec2 uv=gl_TexCoord[0].xy; vec4 base=texture2D(baseTexture,uv);"
  "vec3 g=normalize(N); vec3 n=g;"
  "if(bump>0.){"
  "vec2 dx=dFdx(uv)/sourceTexel,dy=dFdy(uv)/sourceTexel;"
  "float lod=.5*log2(max(1.,min(dot(dx,dx),dot(dy,dy))));"
  "float bias=max(0.,heightLod-lod);"
  "float hs=.5*(heightAt(uv+vec2(texel.x,0.),bias)-heightAt(uv-vec2(texel.x,0.),bias));"
  "float ht=.5*(heightAt(uv+vec2(0.,texel.y),bias)-heightAt(uv-vec2(0.,texel.y),bias));"
  "n=normalize(g-bump*(hs*normalize(S)+ht*normalize(T)));}"
  "vec3 sum=vec3(0.);"
  "for(int i=0;i<4;i++){vec3 d=lightPos[i].xyz-P; float len=length(d)+0.001; vec3 l=d/len;"
  "float a=clamp(1.-len*lightPos[i].w,0.,1.); a=a*a*(3.-2.*a);"
  "float face=step(0.,dot(g,l));"
  "sum+=lightCol[i].rgb*a*face*(0.3+0.7*max(dot(n,l),0.));}"
  "gl_FragColor=vec4(base.rgb*sum,0.);}";
 static const char *vertex=
  "#version 110\n"
  "varying vec3 P,N,S,T;\n"
  "void main(){gl_Position=ftransform(); P=(gl_ModelViewMatrix*gl_Vertex).xyz;"
  "N=gl_NormalMatrix*gl_Normal; S=gl_NormalMatrix*gl_MultiTexCoord1.xyz; T=gl_NormalMatrix*gl_MultiTexCoord2.xyz;"
  "gl_TexCoord[0]=gl_MultiTexCoord0;}";
 int i;
 if (!ri.Cvar_Get("bdgn_modern_lighting_cpu","0",0)->value ||
     ri.Cvar_Get("bdgn_legacy_graphics","0",0)->value ||
     ri.Cvar_Get("vid_rendition","0",0)->value ||
     (Q_stricmp(gl_driver->string,"opengl32") && Q_stricmp(gl_driver->string,"opengl32.dll")) ||
     !gl_config.version_string || atoi(gl_config.version_string)<2) return;
 SCALE_PROC(pixel_uniform4f,"glUniform4f",NULL);
 SCALE_PROC(pixel_uniform1f,"glUniform1f",NULL);
 SCALE_PROC(post_uniform1i,"glUniform1i",NULL);
 SCALE_PROC(pixel_texcoord3,"glMultiTexCoord3f","glMultiTexCoord3fARB");
 if (!pixel_uniform4f || !pixel_uniform1f || !post_uniform1i) return;
 GL_InitModernWater();
 if (!pixel_texcoord3) return;
 pixel_light_program=ScaleCompileShader(source,vertex);
 if (!pixel_light_program) return;
 for (i=0;i<PIXEL_MAX_LIGHTS;i++) {
  char name[32];
  Com_sprintf(name,sizeof(name),"lightPos[%d]",i); pixel_pos_loc[i]=uniform_location(pixel_light_program,name);
  Com_sprintf(name,sizeof(name),"lightCol[%d]",i); pixel_col_loc[i]=uniform_location(pixel_light_program,name);
 }
 pixel_sampler=uniform_location(pixel_light_program,"baseTexture");
 pixel_texel=uniform_location(pixel_light_program,"texel");
 pixel_height_lod=uniform_location(pixel_light_program,"heightLod");
 pixel_source_texel=uniform_location(pixel_light_program,"sourceTexel");
 pixel_bump_loc=uniform_location(pixel_light_program,"bump");
 pixel_lighting=ri.Cvar_Get("gl_pixel_lighting","0",CVAR_ARCHIVE);	/* 0 off, else number of lights (1..4) */
 pixel_bump=ri.Cvar_Get("gl_pixel_bump","1",CVAR_ARCHIVE);		/* 0 flat, 1 relief from the texture */
 pixel_models=ri.Cvar_Get("gl_pixel_models","1",CVAR_ARCHIVE);	/* 1 also light alias models per pixel */
 ri.Cvar_SetValue("bdgn_gl_pixel_lighting_capable",1);
 GL_InitRelief();	/* BDGN 3.55ah */
 GL_InitGodRays();	/* BDGN 3.55ak */
 GL_InitWaterReflection();
#endif
}

/*
=================
BDGN 3.55o: water reflection

Before the main view, the scene above the nearest water plane of the last frame is drawn from the mirrored eye
(pitch and roll negated, clip plane at the water) into a texture of half (gl_water_reflect 1) or full (2) view
size. World, sky and models; no particles, translucent surfaces, view weapon or shadows. Every water surface on
that plane then gets the reflection blended over it in screen space, flipped vertically, with small waves and
more reflection at grazing angles (Fresnel). Lava is not mirrored. Needs GLSL and framebuffer objects; the
capability is bdgn_gl_water_reflect_capable.
=================
*/
static void GL_InitModernWater(void)
{
#ifdef _WIN32
 static const char *vertex =
  "#version 110\n"
  "varying vec3 point; varying vec2 uv; varying vec4 tint;"
  "void main(){gl_Position=ftransform();point=gl_Vertex.xyz;uv=gl_MultiTexCoord0.xy;tint=gl_Color;}";
 static const char *fragment =
  "#version 110\n"
  "uniform sampler2D surfaceTex; uniform vec4 eyeTime; uniform vec4 ripples[4];"
  "varying vec3 point; varying vec2 uv; varying vec4 tint;"
  "void main(){vec2 p=point.xy;vec2 wave=uv*64.;float t=eyeTime.w;"
  "float a=dot(wave,vec2(.048,.027))-t*2.2;"
  "float b=dot(wave,vec2(-.037,.074))-t*1.7;"
  "float c=dot(wave,vec2(.163,-.128))+t*2.9;"
  "vec2 slope=vec2(.48,.27)*cos(a)+vec2(-.34,.68)*cos(b)*.5+vec2(.32,-.25)*cos(c)*.22;"
  "float ringLight=0.;"
  "for(int i=0;i<4;i++){vec4 r=ripples[i];"
  "if(r.w>0.){vec2 d=p-r.xy; float dist=length(d); float front=dist-r.z*104.;"
  "float envelope=smoothstep(-47.,-5.,front)*(1.-smoothstep(0.,9.,front))*r.w;"
  "float ring=cos(front*.32)*envelope;"
  "slope+=d/max(dist,1.)*ring*1.4;ringLight+=max(0.,ring)*.24;}}"
  "vec3 n=normalize(vec3(-slope*1.1,1.));"
  "vec3 eye=normalize(eyeTime.xyz-point+vec3(0.,0.,.001));"
  "vec3 halfLight=normalize(eye+vec3(.36,.22,.9));"
  "float shine=pow(max(dot(n,halfLight),0.),16.);"
  "float edge=1.-abs(dot(n,eye));"
  "vec3 tex=texture2D(surfaceTex,uv+slope*.16).rgb;"
  "float crest=smoothstep(.42,.94,.5+.29*sin(a)+.17*sin(b));"
  "vec3 water=tex*(.78+.26*crest)+vec3(.36,.72,.84)*(shine*.37+edge*edge*.16+crest*.09+ringLight);"
  "gl_FragColor=vec4(water*tint.rgb,tint.a);}";
 static const char *names[]={"ripples[0]","ripples[1]","ripples[2]","ripples[3]"};
 int i;
 if (!scale_fbo_available) return;
 water_program=ScaleCompileShader(fragment,vertex);
 if (!water_program) return;
 water_tex_loc=uniform_location(water_program,"surfaceTex");
 water_view_loc=uniform_location(water_program,"eyeTime");
 for (i=0;i<4;i++) water_ripple_loc[i]=uniform_location(water_program,names[i]);
 water_surface=ri.Cvar_Get("gl_water_surface","0",CVAR_ARCHIVE);
 water_interaction=ri.Cvar_Get("gl_water_interaction","0",CVAR_ARCHIVE);
 ri.Cvar_SetValue("bdgn_gl_water_surface_capable",1);
#endif
}

#ifdef _WIN32
static void WaterCollectRipples(void)
{
 int i;
 water_view_ripple_count=0;
 for (i=r_newrefdef.num_entities-1;i>=0 && water_view_ripple_count<MAX_WATER_RIPPLES;i--)
 {
  const entity_t *e=&r_newrefdef.entities[i];
  if (e->flags&RF_WATER_RIPPLE)
   water_view_ripples[water_view_ripple_count++]=e;
 }
}
#endif

qboolean GL_DrawModernWater(msurface_t *fa)
{
#ifdef _WIN32
 glpoly_t *poly;
 const char *name;
 float height, time, scroll, *v, alpha;
 int count, i, j;
 if (!water_program || !water_surface->value || gl_reflect_pass ||
     !fa->polys || !fa->plane || !fa->texinfo || !fa->texinfo->image)
  return false;
 name=fa->texinfo->image->name;
 if (strstr(name,"lava") || strstr(name,"slime")) return false;
 height=fa->polys->verts[0][2];
 time=r_newrefdef.time;
 scroll=(fa->texinfo->flags & SURF_FLOWING) ? -(time*0.5f-(int)(time*0.5f)) : 0;

 use_program(water_program);
 post_uniform1i(water_tex_loc,0);
 pixel_uniform4f(water_view_loc,r_origin[0],r_origin[1],r_origin[2],time);
 count=0;
 if (water_interaction->value && fabs(fa->plane->normal[2])>=0.99f)
 {
  if (water_view_ripple_count<0) WaterCollectRipples();
  for (i=0;i<water_view_ripple_count && count<4;i++)
  {
   const entity_t *e=water_view_ripples[i];
   if (fabs(e->origin[2]-height)>12) continue;
   alpha=1-e->alpha/1.5f;
   pixel_uniform4f(water_ripple_loc[count++],e->origin[0],e->origin[1],e->alpha,alpha);
  }
 }
 while (count<4) pixel_uniform4f(water_ripple_loc[count++],0,0,0,0);

 for (poly=fa->polys;poly;poly=poly->next)
 {
  qglBegin(GL_TRIANGLE_FAN);
  for (j=0,v=poly->verts[0];j<poly->numverts;j++,v+=VERTEXSIZE)
  {
   qglTexCoord2f(v[3]*(1.0f/64)+scroll,v[4]*(1.0f/64));
   qglVertex3fv(v);
  }
  qglEnd();
 }
 use_program(0);
 water_drawn_surfaces++;
 return true;
#else
 return false;
#endif
}

void GL_InitWaterReflection(void)
{
#ifdef _WIN32
 static const char *fragment=
  "#version 110\n"
  "uniform sampler2D reflectTex; uniform vec4 view; uniform vec3 eye; uniform float time,strength;\n"
  "varying vec3 W;\n"
  "void main(){vec2 uv=(gl_FragCoord.xy-view.xy)/view.zw; uv.y=1.0-uv.y;"
  "uv+=vec2(sin(W.x*0.05+time*1.7)+sin(W.y*0.071+time*1.3),cos(W.y*0.05+time*1.5)+cos(W.x*0.063+time*1.1))*0.004;"
  "vec3 v=normalize(eye-W); float f=1.0-clamp(v.z,0.0,1.0); f=0.25+0.75*f*f;"
  "gl_FragColor=vec4(texture2D(reflectTex,clamp(uv,0.002,0.998)).rgb,strength*f);}";
 static const char *vertex=
  "#version 110\n"
  "varying vec3 W;\n"
  "void main(){gl_Position=ftransform(); W=gl_Vertex.xyz;}";
 water_reflect=ri.Cvar_Get("gl_water_reflect","0",CVAR_ARCHIVE);	/* 0 off, 1 half size, 2 full size */
 ri.Cvar_SetValue("bdgn_gl_water_reflect_capable",0);
 if (!scale_fbo_available || !qglClipPlane) return;
 SCALE_PROC(reflect_uniform3f,"glUniform3f",NULL);
 if (!reflect_uniform3f) return;
 reflect_program=ScaleCompileShader(fragment,vertex);
 if (!reflect_program) return;
 reflect_loc_tex=uniform_location(reflect_program,"reflectTex");
 reflect_loc_view=uniform_location(reflect_program,"view");
 reflect_loc_eye=uniform_location(reflect_program,"eye");
 reflect_loc_time=uniform_location(reflect_program,"time");
 reflect_loc_strength=uniform_location(reflect_program,"strength");
 reflect_failed=0;
 ri.Cvar_SetValue("bdgn_gl_water_reflect_capable",1);
#endif
}

static void ScaleClearErrors(void);
static void ReflectRelease(void)
{
#ifdef _WIN32
 if (reflect_fbo && del_fb) del_fb(1,&reflect_fbo);
 if (reflect_rb && del_rb) del_rb(1,&reflect_rb);
 reflect_fbo=reflect_rb=0;
 reflect_w=reflect_h=0;
#endif
}

/* surface on a horizontal plane facing up, below the eye, not lava; its water height */
static qboolean ReflectPlane(msurface_t *fa,float *height)
{
 cplane_t *p=fa->plane;
 float nz,d;
 if (!p) return false;	/* world water: opaque chains and the alpha list (currentmodel is not the world there) */
 nz=(fa->flags&SURF_PLANEBACK)?-p->normal[2]:p->normal[2];
 if (nz<0.99f) return false;
 d=(fa->flags&SURF_PLANEBACK)?-p->dist:p->dist;
 *height=d/nz;
 if (fa->texinfo && fa->texinfo->image && strstr(fa->texinfo->image->name,"lava")) return false;
 return r_origin[2]>*height+1;
}

/* called for every water surface drawn: remembers the nearest plane for the next frame */
void GL_ReflectNoteWater(msurface_t *fa)
{
 float h,dist;
 vec3_t d;
 if (gl_reflect_pass || !water_reflect || !water_reflect->value || !reflect_program || !fa->polys) return;
 if (!ReflectPlane(fa,&h)) return;
 VectorSubtract(fa->polys->verts[0],r_origin,d);
 dist=DotProduct(d,d);
 if (!reflect_next_valid || dist<reflect_next_dist) {
  reflect_next_valid=1; reflect_next_dist=dist; reflect_next_height=h;
 }
}

/* start of the reflection pass: water height of the last frame, or false */
qboolean GL_ReflectBegin(float *height)
{
#ifdef _WIN32
 int w,h,valid=reflect_next_valid;
 reflect_ready=0;
 reflect_height=reflect_next_height;
 reflect_next_valid=0;
 if (!water_reflect || !water_reflect->value || !reflect_program || reflect_failed || !valid) return false;
 if (r_newrefdef.rdflags&(RDF_NOWORLDMODEL|RDF_UNDERWATER) || r_newrefdef.vieworg[2]<=reflect_height+1) return false;
 w=water_reflect->value>=2?r_newrefdef.width:r_newrefdef.width/2;
 h=water_reflect->value>=2?r_newrefdef.height:r_newrefdef.height/2;
 if (w<64) w=64;
 if (h<48) h=48;
 if (!reflect_fbo || w!=reflect_w || h!=reflect_h) {
  ReflectRelease();
  ScaleClearErrors();
  GL_Bind(REFLECT_TEXNUM);
  qglTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,w,h,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,SCALE_CLAMP_EDGE);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,SCALE_CLAMP_EDGE);
  qglGetIntegerv(0x8CA6,&reflect_prev_fb);
  gen_fb(1,&reflect_fbo); gen_rb(1,&reflect_rb);
  bind_rb(SCALE_RB,reflect_rb); alloc_rb(SCALE_RB,SCALE_DEPTH24,w,h);
  bind_fb(SCALE_FB,reflect_fbo);
  attach_tex(SCALE_FB,SCALE_COLOR,GL_TEXTURE_2D,REFLECT_TEXNUM,0);
  attach_rb(SCALE_FB,SCALE_DEPTH,SCALE_RB,reflect_rb);
  if (check_fb(SCALE_FB)!=SCALE_COMPLETE || qglGetError()!=GL_NO_ERROR) {
   bind_fb(SCALE_FB,reflect_prev_fb);
   ReflectRelease();
   reflect_failed=1;	/* driver refused: no reflection until the next renderer start */
   ri.Con_Printf(PRINT_ALL,"Water reflection: framebuffer refused by the driver, off\n");
   return false;
  }
  bind_fb(SCALE_FB,reflect_prev_fb);
  reflect_w=w; reflect_h=h;
 }
 qglGetIntegerv(0x8CA6,&reflect_prev_fb);
 qglGetIntegerv(GL_VIEWPORT,reflect_prev_view);
 bind_fb(SCALE_FB,reflect_fbo);
 qglViewport(0,0,reflect_w,reflect_h);
 qglClearColor(0,0,0,1);
 qglClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 qglClearColor (0, 0, 0, 1);
 *height=reflect_height;
 return true;
#else
 return false;
#endif
}

void GL_ReflectViewport(void)
{
 qglViewport(0,0,reflect_w,reflect_h);
}

void GL_ReflectEnd(void)
{
#ifdef _WIN32
 bind_fb(SCALE_FB,reflect_prev_fb);
 qglViewport(reflect_prev_view[0],reflect_prev_view[1],reflect_prev_view[2],reflect_prev_view[3]);
 reflect_ready=1;
#endif
}

/* after a water surface is drawn: blend the reflection over it */
void GL_ReflectDrawWater(msurface_t *fa)
{
#ifdef _WIN32
 glpoly_t *bp;
 float h,*v;
 int i,blend,alpha,water_tex;
 GLint view[4];
 if (!reflect_ready || gl_reflect_pass || !ReflectPlane(fa,&h) || h<reflect_height-1.5f || h>reflect_height+1.5f) return;
 qglGetIntegerv(GL_VIEWPORT,view);
 blend=qglIsEnabled(GL_BLEND);
 alpha=qglIsEnabled(GL_ALPHA_TEST);
 GL_SelectTexture(GL_TEXTURE0);
 water_tex=gl_state.currenttextures[gl_state.currenttmu];	/* the water texture: the chain goes on with it */
 if (alpha) qglDisable(GL_ALPHA_TEST);
 use_program(reflect_program);
 GL_Bind(REFLECT_TEXNUM);
 post_uniform1i(reflect_loc_tex,0);
 pixel_uniform4f(reflect_loc_view,(float)view[0],(float)view[1],(float)view[2],(float)view[3]);
 reflect_uniform3f(reflect_loc_eye,r_origin[0],r_origin[1],r_origin[2]);
 pixel_uniform1f(reflect_loc_time,r_newrefdef.time);
 pixel_uniform1f(reflect_loc_strength,water_reflect->value>=2?0.55f:0.45f);
 qglEnable(GL_BLEND);
 qglBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
 for (bp=fa->polys;bp;bp=bp->next) {
  qglBegin(GL_TRIANGLE_FAN);
  for (i=0,v=bp->verts[0];i<bp->numverts;i++,v+=VERTEXSIZE) qglVertex3fv(v);
  qglEnd();
 }
 use_program(0);
 GL_Bind(water_tex);
 if (!blend) qglDisable(GL_BLEND);
 if (alpha) qglEnable(GL_ALPHA_TEST);
#endif
}

void GL_SelectPixelLight(void)
{
 int i,j,max;
 float score[PIXEL_MAX_LIGHTS];
 gl_pixel_lightmask=0;
 pixel_count=0;
 pixel_eye_frame=-1;
 if (!pixel_light_program || !pixel_lighting->value || !gl_dynamic->value ||
     gl_flashblend->value || gl_fastworld->value || gl_lightmap->value ||
     gl_hack_notextures->value || gl_hack_noimages->value ||
     gl_nobind->value || !r_drawworld->value || r_drawflat->value || GL_FULLBRIGHT() ||
     r_drawdistance>0 || gl_ztrick->value || !r_worldmodel || !r_worldmodel->lightdata ||
     (r_newrefdef.rdflags&RDF_NOWORLDMODEL)) return;
 max=(int)pixel_lighting->value;
 if (max<1) max=1;
 if (max>PIXEL_MAX_LIGHTS) max=PIXEL_MAX_LIGHTS;
 for (i=0;i<r_newrefdef.num_dlights && i<32;i++) {
  const dlight_t *light=&r_newrefdef.dlights[i];
  vec3_t delta;
  float s;
  if (light->intensity<=64 || light->color[0]<0 ||
      light->color[1]<0 || light->color[2]<0) continue;
  VectorSubtract(light->origin,r_newrefdef.vieworg,delta);
  s=light->intensity-0.25f*VectorLength(delta);
  if (s<=0) continue;
  /* keep the strongest lights, sorted */
  for (j=pixel_count;j>0 && score[j-1]<s;j--) {
   if (j<max) { score[j]=score[j-1]; pixel_index[j]=pixel_index[j-1]; }
  }
  if (j<max) {
   score[j]=s; pixel_index[j]=i;
   if (pixel_count<max) pixel_count++;
  }
 }
 for (i=0;i<pixel_count;i++) gl_pixel_lightmask|=1<<pixel_index[i];
}

/* Light positions in eye space (r_world_matrix is valid after R_SetupGL). */
static void PixelLightEyeSpace(void)
{
 int i;
 if (pixel_eye_frame==r_framecount) return;
 pixel_eye_frame=r_framecount;
 for (i=0;i<pixel_count;i++) {
  const dlight_t *light=&r_newrefdef.dlights[pixel_index[i]];
  const float *m=r_world_matrix, *o=light->origin;
  pixel_eye[i][0]=m[0]*o[0]+m[4]*o[1]+m[8]*o[2]+m[12];
  pixel_eye[i][1]=m[1]*o[0]+m[5]*o[1]+m[9]*o[2]+m[13];
  pixel_eye[i][2]=m[2]*o[0]+m[6]*o[1]+m[10]*o[2]+m[14];
  pixel_eye[i][3]=1.0f/light->intensity;
 }
}

/* Binds the program with the frame's lights; scale = light colour factor (models use gl_modulate like R_LightPoint). */
static void PixelLightBegin(float scale,float bump)
{
 int i;
 PixelLightEyeSpace();
 GL_EnableMultitexture(false);
 qglDisable(GL_ALPHA_TEST);
 qglDepthMask(GL_FALSE);
 qglEnable(GL_BLEND);
 qglBlendFunc(GL_ONE,GL_ONE);
 use_program(pixel_light_program);
 post_uniform1i(pixel_sampler,0);
 pixel_uniform1f(pixel_bump_loc,bump);
 for (i=0;i<PIXEL_MAX_LIGHTS;i++) {
  if (i<pixel_count) {
   const dlight_t *light=&r_newrefdef.dlights[pixel_index[i]];
   float k=light->intensity*scale/256;
   pixel_uniform4f(pixel_pos_loc[i],pixel_eye[i][0],pixel_eye[i][1],pixel_eye[i][2],pixel_eye[i][3]);
   pixel_uniform4f(pixel_col_loc[i],light->color[0]*k,light->color[1]*k,light->color[2]*k,0);
  } else {
   pixel_uniform4f(pixel_pos_loc[i],0,0,0,1);
   pixel_uniform4f(pixel_col_loc[i],0,0,0,0);
  }
 }
}

static void PixelLightEnd(void)
{
 use_program(0);
 qglBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
 qglDisable(GL_BLEND);
 qglDepthMask(GL_TRUE);
 qglDepthFunc(GL_LEQUAL);
 qglEnable(GL_ALPHA_TEST);
}

void GL_DrawPixelLight(void)
{
 msurface_t *surface;
 int i,j;
 image_t *bound=NULL;
 float bump;
 if (!gl_pixel_lightmask || gl_ztrick->value) return;
 bump=pixel_bump->value>0?4.0f*pixel_bump->value:0;
 qglMatrixMode(GL_MODELVIEW);
 qglLoadMatrixf(r_world_matrix);	/* the shader works in eye space */
 PixelLightBegin(1.0f,bump);
 /* 3.60i hotfix: fixed-function base and GLSL overlay need not round depth
    identically (especially MSAA and stair movement). Match the model path. */
 qglDepthFunc(GL_LEQUAL);
 qglEnable(GL_POLYGON_OFFSET_FILL);
 qglPolygonOffset(-1,-2);
 for (i=0,surface=r_worldmodel->surfaces;i<r_worldmodel->numsurfaces;i++,surface++) {
  glpoly_t *poly=surface->polys;
  image_t *image;
  float side,sign;
  const float *n,*sv,*tv;
  if (surface->visframe!=r_framecount || !poly ||
      surface->dlightframe!=r_framecount || !(surface->dlightbits&gl_pixel_lightmask) ||
      (surface->texinfo->flags&(SURF_SKY|SURF_WARP|SURF_TRANS33|SURF_TRANS66|SURF_FLOWING))) continue;
  side=DotProduct(r_newrefdef.vieworg,surface->plane->normal)-surface->plane->dist;
  if ((side<0)!=((surface->flags&SURF_PLANEBACK)!=0)) continue;
  /* R_DrawWorld's temporary currententity has gone out of scope. Use the
     same explicit world frame as the base pass, never the stale pointer. */
  image=R_TextureAnimationFrame(surface->texinfo,(int)(r_map_animation_time*2));
  if (!image || image->width<=0 || image->height<=0) continue;
  if (image!=bound) {
   int width,height,logical_width,logical_height,lod=0;
   GL_Bind(image->texnum);
   width=image->upload_width>0?image->upload_width:image->width;
   height=image->upload_height>0?image->upload_height:image->height;
   uniform2f(pixel_source_texel,1.0f/width,1.0f/height);
   logical_width=image->width<width?image->width:width;
   logical_height=image->height<height?image->height:height;
   while (width>logical_width || height>logical_height) {
    if (width>1) width/=2;
    if (height>1) height/=2;
    lod++;
   }
   uniform2f(pixel_texel,1.0f/logical_width,1.0f/logical_height);
   pixel_uniform1f(pixel_height_lod,(float)lod);
   bound=image;
  }
  sign=(surface->flags&SURF_PLANEBACK)?-1.0f:1.0f;
  n=surface->plane->normal; sv=surface->texinfo->vecs[0]; tv=surface->texinfo->vecs[1];
  qglNormal3f(n[0]*sign,n[1]*sign,n[2]*sign);
  pixel_texcoord3(0x84C1,sv[0],sv[1],sv[2]);
  pixel_texcoord3(0x84C2,tv[0],tv[1],tv[2]);
  qglBegin(GL_POLYGON);
  for (j=0;j<poly->numverts;j++) {
   float *v=poly->verts[j];
   qglTexCoord2f(v[3],v[4]);
   qglVertex3fv(v);
  }
  qglEnd();
 }
 qglPolygonOffset(0,0);
 qglDisable(GL_POLYGON_OFFSET_FILL);
 PixelLightEnd();
}

/* BDGN 3.55ah: 3D surface structure (gl_relief). The world's walls and floors get depth from their own texture:
   relief (parallax occlusion) mapping. The height is the brightness of whichever texture set is loaded (original,
   HD, AI, 4K), so the structure always fits the texture pack in use and needs no files of its own. The eye sees into
   the surface: bricks stand out, joints and cracks lie deeper and are hidden behind the raised parts at flat angles;
   a little shading from the relief on top. Nothing in the map changes: no collision, nothing a server could see.
   How: after the world is drawn the normal way, the visible world surfaces near the viewer (fades out at 350-700
   units) are drawn once more at the same depth with a shader that looks the texture up through the relief and
   multiplies it with the surface's lightmap. Surfaces lit by a dynamic light this frame keep the normal picture
   (their lightmap is a temporary one). Moving brush models (doors, lifts), water, sky and glass are not touched.
   Modern drivers only (GLSL, same conditions as per-pixel lighting): bdgn_gl_relief_capable. */
static GLuint relief_program;
static GLint relief_base,relief_lm,relief_texel,relief_depth_loc,relief_shade_loc;
static cvar_t *gl_relief,*gl_relief_depth;

static void GL_InitRelief(void)
{
 static const char *source=
  "#version 110\n"
  "uniform sampler2D baseTexture,lightMap; uniform vec2 texel; uniform float depth,shade;\n"
  "varying vec3 P,N,S,T;\n"
  /* Filter the height signal separately: 4K colour noise is not geometry. */
  "float H(vec2 uv){return dot(texture2D(baseTexture,uv,1.0).rgb,vec3(0.299,0.587,0.114));}\n"
  "void main(){\n"
  "vec2 uv=gl_TexCoord[0].xy; vec3 n=normalize(N); vec3 V=normalize(-P);\n"
  "float fade=clamp(1.-(length(P)-350.)/350.,0.,1.);\n"
  "float vn=max(dot(V,n),0.3);\n"
  "vec3 w=(V-n*dot(V,n))/vn;\n"
  "vec2 duv=vec2(dot(w,S),dot(w,T))*depth*fade;\n"
  /* Uniform iteration/sample flow keeps implicit texture derivatives defined.
     The old data-dependent sample branches shimmered as the eye moved. */
  "float t=0.; float st=0.0625;\n"
  "for(int i=0;i<16;i++){float h=1.-H(uv-duv*t);t+=st*(1.-step(h,t));}\n"
  "float t0=max(0.,t-st);\n"
  "float d1=(1.-H(uv-duv*t))-t; float d0=(1.-H(uv-duv*t0))-t0;\n"
  "t=t0+st*clamp(d0/max(d0-d1,0.0001),0.,1.);\n"
  "uv=uv-duv*t;\n"
  "vec4 base=texture2D(baseTexture,uv);\n"
  "float h0=H(uv);\n"
  "float hs=H(uv+vec2(texel.x*1.5,0.))-h0; float ht=H(uv+vec2(0.,texel.y*1.5))-h0;\n"
  "vec3 bn=normalize(n-6.0*(hs*normalize(S)+ht*normalize(T)));\n"
  "vec3 L=normalize(V+vec3(0.,0.7,0.));\n"
  "float s=1.+shade*fade*(1.6*(dot(bn,L)-dot(n,L))-0.22*(1.-h0));\n"
  /* 3.55al: the result is not drawn over the surface but multiplied into it (blend dst*src*2): relief colour divided
     by the flat texture colour that is already there. The light in the picture - static, dynamic, flickering - stays. */
  "vec3 old=texture2D(baseTexture,gl_TexCoord[0].xy).rgb;\n"
  "vec3 r=(base.rgb*clamp(s,0.55,1.35)+0.03)/(old+0.03);\n"
  "gl_FragColor=vec4(clamp(r*0.5,0.,1.),1.);\n"
  "}\n";
 static const char *vertex=
  "#version 110\n"
  "varying vec3 P,N,S,T;\n"
  "void main(){gl_Position=ftransform(); P=(gl_ModelViewMatrix*gl_Vertex).xyz;\n"
  "N=gl_NormalMatrix*gl_Normal; S=gl_NormalMatrix*gl_MultiTexCoord2.xyz; T=gl_NormalMatrix*gl_MultiTexCoord3.xyz;\n"
  "gl_TexCoord[0]=gl_MultiTexCoord0; gl_TexCoord[1]=gl_MultiTexCoord1;}\n";
 relief_program=0;
 gl_relief=ri.Cvar_Get("gl_relief","0",CVAR_ARCHIVE);		/* 0 off, 1 on */
 gl_relief_depth=ri.Cvar_Get("gl_relief_depth","1.5",CVAR_ARCHIVE);	/* depth of the relief in map units (0.5 .. 4) */
 ri.Cvar_SetValue("bdgn_gl_relief_capable",0);
 SCALE_PROC(post_active_texture,"glActiveTexture","glActiveTextureARB");
 if (!post_active_texture || !pixel_texcoord3 || !pixel_uniform1f || !post_uniform1i) return;
 relief_program=ScaleCompileShader(source,vertex);
 if (!relief_program) return;
 relief_base=uniform_location(relief_program,"baseTexture");
 relief_lm=uniform_location(relief_program,"lightMap");
 relief_texel=uniform_location(relief_program,"texel");
 relief_depth_loc=uniform_location(relief_program,"depth");
 relief_shade_loc=uniform_location(relief_program,"shade");
 ri.Cvar_SetValue("bdgn_gl_relief_capable",1);
}

/* state of one relief pass (world or brush models) */
static int relief_first,relief_lmbound;
static image_t *relief_image;

static int ReliefWanted(void)
{
 return relief_program && gl_relief && gl_relief->value && r_worldmodel && !gl_reflect_pass && !gl_ztrick->value &&
     !(r_newrefdef.rdflags&RDF_NOWORLDMODEL) && !r_fullbright->value && !gl_lightmap->value &&
     !ri.Cvar_Get("gl_fastworld","0",0)->value && !ri.Cvar_Get("r_drawflat","0",0)->value &&
     !ri.Cvar_Get("gl_hack_notextures","0",0)->value && !ri.Cvar_Get("gl_hack_noimages","0",0)->value &&
     !ri.Cvar_Get("gl_drawdist","0",0)->value;
}

static void ReliefEnd(void)
{
 if (relief_first) return;
 use_program(0);
 qglBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
 qglDisable(GL_BLEND);
 qglDepthMask(GL_TRUE);
 qglDepthFunc(GL_LEQUAL);
 qglPolygonOffset(0,0);
 qglDisable(GL_POLYGON_OFFSET_FILL);
 qglEnable(GL_ALPHA_TEST);
}

/* org: the eye in the space of the surfaces; world: only surfaces marked visible this frame */
static void ReliefSurfaces(msurface_t *surface,int count,const float *org,int world,int frame)
{
 int i,j;
 float depth=gl_relief_depth->value;
 if (depth<0.5f) depth=0.5f;
 if (depth>4) depth=4;
 for (i=0;i<count;i++,surface++) {
  glpoly_t *poly=surface->polys;
  image_t *image;
  float side,sign,iw,ih;
  const float *n,*sv,*tv;
  if ((world && surface->visframe!=r_framecount) || !poly ||
      (surface->texinfo->flags&(SURF_SKY|SURF_WARP|SURF_TRANS33|SURF_TRANS66|SURF_FLOWING|SURF_NODRAW)) ||
      (surface->flags&(SURF_DRAWTURB|SURF_DRAWSKY))) continue;
  side=DotProduct(org,surface->plane->normal)-surface->plane->dist;
  if ((side<0)!=((surface->flags&SURF_PLANEBACK)!=0)) continue;
  if (side>700 || side<-700) continue;	/* beyond the fade */
  image=R_TextureAnimationFrame(surface->texinfo,frame);
  if (!image || image->width<=0 || image->height<=0) continue;
  if (relief_first) {
   relief_first=0;
   GL_EnableMultitexture(false);
   qglDisable(GL_ALPHA_TEST);
   qglEnable(GL_BLEND);
   qglBlendFunc(GL_DST_COLOR,GL_SRC_COLOR);	/* 3.55al: picture * result * 2 */
   qglColor4f(1,1,1,1);
   qglDepthMask(GL_FALSE);
   qglDepthFunc(GL_LEQUAL);
   qglEnable(GL_POLYGON_OFFSET_FILL);
   qglPolygonOffset(-1,-2);
   use_program(relief_program);
   post_uniform1i(relief_base,0);
   post_uniform1i(relief_lm,1);
   pixel_uniform1f(relief_depth_loc,depth);
   pixel_uniform1f(relief_shade_loc,1.0f);
  }
  if (image!=relief_image) {
   GL_Bind(image->texnum);
   uniform2f(relief_texel,1.0f/(image->upload_width>0?image->upload_width:image->width),
    1.0f/(image->upload_height>0?image->upload_height:image->height));
   relief_image=image;
  }
  sign=(surface->flags&SURF_PLANEBACK)?-1.0f:1.0f;
  n=surface->plane->normal; sv=surface->texinfo->vecs[0]; tv=surface->texinfo->vecs[1];
  iw=1.0f/surface->texinfo->image->width; ih=1.0f/surface->texinfo->image->height;	/* texture coordinates per map unit */
  qglNormal3f(n[0]*sign,n[1]*sign,n[2]*sign);
  pixel_texcoord3(0x84C2,sv[0]*iw,sv[1]*iw,sv[2]*iw);
  pixel_texcoord3(0x84C3,tv[0]*ih,tv[1]*ih,tv[2]*ih);
  qglBegin(GL_POLYGON);
  for (j=0;j<poly->numverts;j++) {
   float *v=poly->verts[j];
   qglTexCoord2f(v[3],v[4]);
   qglVertex3fv(v);
  }
  qglEnd();
 }
}

void GL_DrawRelief(void)
{
 if (!ReliefWanted()) return;
 relief_first=1; relief_lmbound=-1; relief_image=NULL;
 qglMatrixMode(GL_MODELVIEW);
 qglLoadMatrixf(r_world_matrix);
 ReliefSurfaces(r_worldmodel->surfaces,r_worldmodel->numsurfaces,r_newrefdef.vieworg,1,(int)(r_map_animation_time*2));
 ReliefEnd();
}

/* BDGN 3.55aj: the same pass for doors, lifts and other moving brush models. Called after the entities are drawn;
   the model is set up exactly as R_DrawBrushModel does it, so the depth of every pixel is the same. */
void GL_DrawReliefModels(void)
{
 int i,k;
 if (!ReliefWanted() || !r_drawentities->value) return;
 relief_first=1; relief_lmbound=-1; relief_image=NULL;
 qglMatrixMode(GL_MODELVIEW);
 for (i=0;i<r_newrefdef.num_entities;i++) {
  entity_t *e=&r_newrefdef.entities[i];
  model_t *model=e->model;
  vec3_t mins,maxs,org;
  int rotated;
  if (!model || model->type!=mod_brush || model==r_worldmodel || !model->nummodelsurfaces ||
      (e->flags&(RF_TRANSLUCENT|RF_BEAM))) continue;
  rotated=e->angles[0] || e->angles[1] || e->angles[2];
  for (k=0;k<3;k++) {
   mins[k]=e->origin[k]+(rotated?-model->radius:model->mins[k]);
   maxs[k]=e->origin[k]+(rotated?model->radius:model->maxs[k]);
  }
  if (R_CullBox(mins,maxs)) continue;
  VectorSubtract(r_newrefdef.vieworg,e->origin,org);
  if (rotated) {
   vec3_t temp,forward,right,up;
   VectorCopy(org,temp);
   R_AngleVectors(e->angles,forward,right,up);
   org[0]=DotProduct(temp,forward); org[1]=-DotProduct(temp,right); org[2]=DotProduct(temp,up);
  }
  qglLoadMatrixf(r_world_matrix);
  e->angles[0]=-e->angles[0]; e->angles[2]=-e->angles[2];	/* as R_DrawBrushModel */
  R_RotateForEntity(e);
  e->angles[0]=-e->angles[0]; e->angles[2]=-e->angles[2];
  ReliefSurfaces(&model->surfaces[model->firstmodelsurface],model->nummodelsurfaces,org,0,e->frame);
 }
 qglLoadMatrixf(r_world_matrix);
 ReliefEnd();
}

/* BDGN 3.55h: true when the per-pixel pass will light this alias model (its lights are then left out of the
   per-vertex R_LightPoint sum). */
int GL_PixelLightModelMask(const entity_t *e,float radius)
{
 int i,mask=0;
 if (!gl_pixel_lightmask || !pixel_models || !pixel_models->value) return 0;
 for (i=0;i<pixel_count;i++) {
  const dlight_t *light=&r_newrefdef.dlights[pixel_index[i]];
  vec3_t d;
  VectorSubtract(light->origin,e->origin,d);
  if (VectorLength(d)<light->intensity+radius) mask|=1<<pixel_index[i];
 }
 return mask;
}

/* Additive pass over an alias model already drawn with the current modelview; verts/normals in model space. */
void GL_DrawPixelLightModel(const float (*xyz)[4],const float (*normal)[3],const int *order)
{
 int count;
 PixelLightBegin(gl_modulate->value,0);
 qglDepthFunc(GL_LEQUAL);
 qglEnable(GL_POLYGON_OFFSET_FILL);
 qglPolygonOffset(-1,-2);	/* the base pass may come from another lerp path; stay on its surface */
 pixel_texcoord3(0x84C1,0,0,0);
 pixel_texcoord3(0x84C2,0,0,0);
 while ((count=*order++)!=0) {
  if (count<0) { count=-count; qglBegin(GL_TRIANGLE_FAN); }
  else qglBegin(GL_TRIANGLE_STRIP);
  do {
   int index=order[2];
   qglTexCoord2f(((const float *)order)[0],((const float *)order)[1]);
   order+=3;
   qglNormal3fv(normal[index]);
   qglVertex3fv(xyz[index]);
  } while (--count);
  qglEnd();
 }
 qglPolygonOffset(0,0);
 qglDisable(GL_POLYGON_OFFSET_FILL);
 PixelLightEnd();
}

static float SceneGamma(void)
{
 float gamma=post_brightness->value;
 if (!(gamma>=0.5f)) return 0.5f;
 if (gamma>1.3f) return 1.3f;
 return gamma;
}

static void PostReport(int active)
{
 if(post_reported==active) return;
 post_reported=active;
 ri.Cvar_SetValue("bdgn_gl_post_active",active);
}

static gl_scale_tile_t scale_tiles[SCALE_TILE_LIMIT];
static int scale_count,scale_width,scale_height,scale_backend,scale_checked;
static int scale_report_width,scale_report_height,scale_report_backend;
static int pixel_view_x,pixel_view_y,pixel_view_width,pixel_view_height;
static GLint copy_framebuffer_rgb[3],copy_texture_rgb[3],copy_internal_format;
static qboolean scale_failed;
static byte *scale_pixels;
static qboolean scale_info_registered;

void GL_ResetWaterDrawCount(void)
{
 water_drawn_surfaces=0;
}

static void GL_ScaleInfo_f(void)
{
 static const char *paths[]={"native","OpenGL copy","CPU readback","FBO","direct pixel copy"};

 ri.Con_Printf(PRINT_ALL,"Render Scale driver: %s / %s / OpenGL %s\n",
  gl_driver->string,
  gl_config.renderer_string?(const char *)gl_config.renderer_string:"unknown",
  gl_config.version_string?(const char *)gl_config.version_string:"unknown");
 ri.Con_Printf(PRINT_ALL,"Render Scale: requested %d%%, render %dx%d, output %dx%d, path %d (%s)\n",
  RenderScalePercent(RenderScaleStep(scale_value->value)),scale_report_width,scale_report_height,
  vid.width,vid.height,scale_report_backend,paths[scale_report_backend]);
 ri.Con_Printf(PRINT_ALL,"FBO functions: %s, legacy copy: %s\n",
  scale_fbo_available?"available":"unavailable",scale_legacy->value?"enabled":"disabled");
 ri.Con_Printf(PRINT_ALL,"OpenGL post: FXAA %s, Bloom %s, SSAO %s, Adaptive upscale %s (active mask %d)\n",
  post_available&POST_FXAA?"available":"unavailable",post_available&POST_BLOOM?"available":"unavailable",
  post_available&POST_SSAO?"available":"unavailable",post_available&POST_ADAPTIVE?"available":"unavailable",post_reported);
 ri.Con_Printf(PRINT_ALL,"Filmic tone mapping: %s; colour grading: %s\n",
  post_available&POST_TONEMAP?"available":"unavailable",post_available&POST_GRADE?"available":"unavailable");
 ri.Con_Printf(PRINT_ALL,"Modern water: shader %s, waves %s, ripples %s, drawn surfaces in last view: %d\n",
  water_program?"available":"unavailable",water_program && water_surface->value?"on":"off",
  water_program && water_surface->value && water_interaction->value?"on":"off",water_drawn_surfaces);
 ri.Con_Printf(PRINT_ALL,"Heat haze: weapon beams %d, scene copy %s\n",
  weapon_haze_beams,haze_copy_failed?"failed":"OK");
 ri.Con_Printf(PRINT_ALL,"Direct pixel copy test: %s\n",
  scale_pixelcopy->value?"enabled (fullscreen, Sharp)":"disabled");
 ri.Con_Printf(PRINT_ALL,"Copy texture format test: %s\n",scale_copyformat->value?"enabled":"disabled");
 if(scale_report_backend==SCALE_COPY || scale_report_backend==SCALE_READBACK)
  ri.Con_Printf(PRINT_ALL,"Copy format: framebuffer RGB %d/%d/%d, texture RGB %d/%d/%d, requested GL format 0x%X\n",
   copy_framebuffer_rgb[0],copy_framebuffer_rgb[1],copy_framebuffer_rgb[2],
   copy_texture_rgb[0],copy_texture_rgb[1],copy_texture_rgb[2],(unsigned)copy_internal_format);
#ifdef _WIN32
 ScaleCheckWGL();
#endif
}

static void ScaleReport(int width,int height,int backend)
{
 if (scale_report_width==width && scale_report_height==height && scale_report_backend==backend) return;
 scale_report_width=width;
 scale_report_height=height;
 scale_report_backend=backend;
 ri.Cvar_SetValue("gl_render_width",width);
 ri.Cvar_SetValue("gl_render_height",height);
 ri.Cvar_SetValue("gl_render_scale_path",backend);
 if(backend==SCALE_NATIVE) {
  reported_filter=0;
  ri.Cvar_SetValue("gl_render_scale_filter_active",0);
 }
}

static void ScaleRelease(void)
{
 int i,j;
 if(scale_fb) {
  bind_fb(SCALE_FB,0);
  del_fb(1,&scale_fb);
  if(scale_depth) del_rb(1,&scale_depth);
  scale_fb=scale_depth=0;
 }
 if(post_depth_texture) {
  qglDeleteTextures(1,&post_depth_texture);
  if(gl_state.currenttextures[0]==(int)post_depth_texture) gl_state.currenttextures[0]=-1;
  if(gl_state.currenttextures[1]==(int)post_depth_texture) gl_state.currenttextures[1]=-1;
  post_depth_texture=0;
 }
 scale_offscreen=false;
 for(i=0;i<scale_count;i++) {
  GLuint texture=scale_tiles[i].texture;
  if (qglDeleteTextures) qglDeleteTextures(1,&texture);
  for(j=0;j<2;j++)
   if (gl_state.currenttextures[j]==(int)texture) gl_state.currenttextures[j]=-1;
 }
 free(scale_pixels);
 scale_pixels=NULL;
 scale_count=scale_width=scale_height=scale_checked=0;
 texture_filter=0;
 scale_backend=SCALE_NATIVE;
}

void GL_ScaleShutdown(void)
{
 if(scale_info_registered) {
  ri.Cmd_RemoveCommand("gl_scale_info");
  scale_info_registered=false;
 }
 ScaleRelease();
 if(scale_program) delete_program(scale_program);
 if(post_program) delete_program(post_program);
 if(pixel_light_program) delete_program(pixel_light_program);
 pixel_light_program=0;
 ReflectRelease();	/* BDGN 3.55o */
 if(water_program) delete_program(water_program);
 water_program=0;
 if(reflect_program) delete_program(reflect_program);
 reflect_program=0; reflect_ready=reflect_next_valid=0; gl_reflect_pass=false;
 gl_pixel_lightmask=0;
 pixel_count=0;
 post_program=0;
 post_available=post_frame=post_target_request=post_rejected_request=0;
 scale_program=0;
 scale_shader_checked=false;
 scale_failed=false;
 rejected_width=rejected_height=0;
 scale_value=NULL;
}

static void ScaleUpdateCapability(void)
{
 ri.Cvar_SetValue("gl_render_scale_capable",scale_fbo_available ||
  (qglReadPixels && ((scale_legacy->value && (qglCopyTexSubImage2D || qglTexSubImage2D)) ||
    (scale_pixelcopy->value && qglCopyPixels && qglPixelZoom) ||
    (scale_copyformat->value && qglCopyTexSubImage2D))));
 scale_legacy->modified=scale_pixelcopy->modified=scale_copyformat->modified=false;
}

/* BDGN 3.55ai: the scene's frame buffer gets a stencil buffer too (packed with the depth buffer) when the window has
   one, so the shadow volumes work with render scaling and the picture effects */
static int scale_packed_ok=1;
int gl_window_stencil=-1;	/* stencil bits of the window (GL_WindowStencil, gl_shadowvol.c) */
static qboolean ScalePrepareFBO(int width,int height,qboolean post,qboolean depth_texture);
static void ScaleClearErrors(void);

static void PostInit(void)
{
 post_fxaa=ri.Cvar_Get("gl_fxaa","0",CVAR_ARCHIVE);
 post_bloom=ri.Cvar_Get("gl_bloom","0",CVAR_ARCHIVE);
 post_ssao=ri.Cvar_Get("gl_ssao","0",CVAR_ARCHIVE);
 post_tonemap=ri.Cvar_Get("gl_tonemap","0",CVAR_ARCHIVE);
 post_grade=ri.Cvar_Get("gl_colour_grade","0",CVAR_ARCHIVE);
 post_brightness=ri.Cvar_Get("gl_scene_gamma","1",CVAR_ARCHIVE);
 post_available=0;
 post_reported=-1;
 PostReport(0);
#ifdef _WIN32
 if(scale_fbo_available && gl_config.version_string && gl_config.version_string[0]>='2' &&
    qglGetError && qglTexImage2D && qglTexParameteri) {
  post_program=ScaleCompileShader(post_shader_source,NULL);
  if(post_program && ScalePrepareFBO(320,240,true,false))
   post_available=POST_FXAA|POST_BLOOM|POST_ADAPTIVE|POST_TONEMAP|POST_GRADE;
  ScaleRelease();
  ScaleClearErrors();
  if(post_available) {
   post_step=uniform_location(post_program,"stepUV");
   post_end=uniform_location(post_program,"endUV");
   post_effects=uniform_location(post_program,"effects");
   post_ao_sharpen=uniform_location(post_program,"aoAndSharpen");
   post_depth_range=uniform_location(post_program,"depthRange");
   post_ao_params=uniform_location(post_program,"aoParams");
   post_sampler=uniform_location(post_program,"sceneDepth");
   post_colour=uniform_location(post_program,"colourStyle");
   post_brightness_loc=uniform_location(post_program,"sceneBrightness");
   if (post_brightness_loc>=0) post_available|=POST_BRIGHTNESS;
   SCALE_PROC(post_active_texture,"glActiveTexture","glActiveTextureARB");
   SCALE_PROC(post_uniform1i,"glUniform1i",NULL);
   if(post_active_texture && post_uniform1i && post_sampler>=0 &&
      ScalePrepareFBO(320,240,true,true))
    post_available|=POST_SSAO;
   ScaleRelease();
   ScaleClearErrors();
  }
 }
#endif
 ri.Cvar_SetValue("bdgn_gl_post_available",post_available);
}

void GL_ScaleInit(void)
{
 scale_value=ri.Cvar_Get("gl_render_scale","100",CVAR_ARCHIVE);
 scale_filter=ri.Cvar_Get("gl_upscale_filter","0",CVAR_ARCHIVE);
 scale_legacy=ri.Cvar_Get("gl_render_scale_legacy","0",CVAR_ARCHIVE);
 scale_pixelcopy=ri.Cvar_Get("gl_render_scale_pixelcopy","0",0);
 scale_copyformat=ri.Cvar_Get("gl_render_scale_copyformat","0",0);
 scale_legacy_graphics=ri.Cvar_Get("bdgn_legacy_graphics","0",0)->value!=0;
 if(scale_legacy_graphics) {
  scale_fbo_available=false;
  post_available=post_frame=0;
  ri.Cvar_SetValue("gl_render_scale_fbo_capable",0);
  ri.Cvar_SetValue("gl_render_scale_capable",0);
  ri.Cvar_SetValue("gl_upscale_shader_capable",0);
  ri.Cvar_SetValue("bdgn_gl_post_available",0);
  ScaleReport(vid.width,vid.height,SCALE_NATIVE);
  return;
 }
 ScaleLoadFBO();
 if(scale_fbo_available) {
  scale_fbo_available=ScalePrepareFBO(320,240,false,false);
  ScaleRelease();
  ScaleClearErrors();
 }
 ri.Cvar_SetValue("gl_render_scale_fbo_capable",scale_fbo_available);
 ScaleUpdateCapability();
 PostInit();
#ifdef _WIN32
 if(gl_config.version_string && gl_config.version_string[0]>='2' &&
    ScaleProc("glCreateShader",NULL)) {
  scale_shader_checked=true;
  ScaleLoadShader();
 }
 ri.Cvar_SetValue("gl_upscale_shader_capable",scale_program!=0);
#endif
 scale_report_width=-1;
 ScaleReport(vid.width,vid.height,SCALE_NATIVE);
 ri.Cmd_AddCommand("gl_scale_info",GL_ScaleInfo_f);
 scale_info_registered=true;
}

static void ScaleFail(void)
{
 ScaleRelease();
 scale_failed=true;
 ri.Cvar_SetValue("gl_render_scale_capable",0);
 ScaleReport(vid.width,vid.height,SCALE_NATIVE);
 ri.Con_Printf(PRINT_ALL,"OpenGL render scale unavailable on this driver. Using native resolution.\n");
}

static int ScalePowerOfTwo(int n)
{
 int size=1;
 while(size<n) size*=2;
 return size;
}

static void ScaleClearErrors(void)
{
 int i;
 if(qglGetError)
  for(i=0;i<16 && qglGetError()!=GL_NO_ERROR;i++) {}
}

static void ScaleQuad(float x,float y,float w,float h,float u,float v,float left,float lower)
{
 qglBegin(GL_QUADS);
 qglTexCoord2f(left,v); qglVertex2f(x,y);
 qglTexCoord2f(u,v); qglVertex2f(x+w,y);
 qglTexCoord2f(u,lower); qglVertex2f(x+w,y+h);
 qglTexCoord2f(left,lower); qglVertex2f(x,y+h);
 qglEnd();
}

static void ScaleCopyPixels(const refdef_t *view,int width,int height)
{
 /* Move a valid raster position to an exact pixel, including the bottom edge. */
 qglRasterPos2f(0,0);
 qglBitmap(0,0,0,0,(float)view->x,(float)-(view->y+view->height),NULL);
 qglPixelZoom((float)view->width/width,(float)view->height/height);
 qglCopyPixels(view->x,vid.height-view->y-height,width,height,GL_COLOR);
 qglPixelZoom(1,1);
}

static qboolean ScalePreparePixelCopy(const refdef_t *view,int width,int height,float ui_scale)
{
 int x,y;
 GLint draw_buffer=0;
 qboolean ok=true;
 byte rgb[4];

 if(!gl_state.fullscreen || !qglCopyPixels || !qglPixelZoom || !qglRasterPos2f ||
  !qglBitmap || !qglReadBuffer || !qglReadPixels || !qglGetError || !qglGetIntegerv)
  return false;
 qglGetIntegerv(GL_DRAW_BUFFER,&draw_buffer);
 if(draw_buffer!=GL_BACK) return false;

 /* Exercise zoom and overlap at the actual resolution. A stub or an
    in-place copy that overwrites its own source fails the color checks. */
 ScaleClearErrors();
 GL_EnableMultitexture(false);
 qglDisable(GL_TEXTURE_2D);
 qglDisable(GL_ALPHA_TEST);
 qglReadBuffer(GL_BACK);
 qglColor4f(0,0,0,1);
 ScaleQuad(view->x/ui_scale,view->y/ui_scale,view->width/ui_scale,view->height/ui_scale,0,0,0,0);
 for(y=0;y<4;y++) for(x=0;x<4;x++) {
  int left=x*width/4,top=y*height/4;
  qglColor4f(x/3.0f,y/3.0f,1,1);
  ScaleQuad((view->x+left)/ui_scale,(view->y+top)/ui_scale,
   ((x+1)*width/4-left)/ui_scale,((y+1)*height/4-top)/ui_scale,0,0,0,0);
 }
 ScaleCopyPixels(view,width,height);
 for(y=0;y<4 && ok;y++) for(x=0;x<4 && ok;x++) {
  memset(rgb,0,sizeof(rgb));
  qglReadPixels(view->x+(2*x+1)*view->width/8,
   vid.height-1-view->y-(2*y+1)*view->height/8,1,1,GL_RGB,GL_UNSIGNED_BYTE,rgb);
  ok=abs(rgb[0]-85*x)<=12 && abs(rgb[1]-85*y)<=12 && rgb[2]>=243;
 }
 qglColor4f(1,1,1,1);
 qglEnable(GL_ALPHA_TEST);
 qglEnable(GL_TEXTURE_2D);
 if(qglGetError()!=GL_NO_ERROR || !ok) return false;

 pixel_view_x=view->x;
 pixel_view_y=view->y;
 pixel_view_width=view->width;
 pixel_view_height=view->height;
 scale_width=width;
 scale_height=height;
 scale_backend=SCALE_PIXEL_COPY;
 return true;
}

static qboolean ScalePrepareFBO(int width,int height,qboolean post,qboolean depth_texture)
{
 int packed=scale_packed_ok && GL_WindowStencil()>=8;	/* BDGN 3.55ai */
 int texw=post?width:ScalePowerOfTwo(width),texh=post?height:ScalePowerOfTwo(height),limit=0;
 GLint viewport_limit[2]={0,0};
 gl_scale_tile_t *tile=&scale_tiles[0];

 if(!scale_fbo_available) return false;
 qglGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
 qglGetIntegerv(GL_MAX_VIEWPORT_DIMS,viewport_limit);
 if(width>viewport_limit[0] || height>viewport_limit[1] || texw>limit||texh>limit||texh<=0||
    (unsigned)texw>(post?POST_MEMORY_LIMIT:SCALE_MEMORY_LIMIT)/8/(unsigned)texh) return false;
 ScaleClearErrors();
 GL_EnableMultitexture(false);
 tile->x=tile->y=0;
 tile->w=width; tile->h=height;
 tile->u=(float)width/texw; tile->v=(float)height/texh;
 tile->texture=SCALE_TEX_BASE;
 scale_count=1;
 /* Allocate the render texture even when gl_nobind redirects normal binds. */
 qglBindTexture(GL_TEXTURE_2D,tile->texture);
 gl_state.currenttextures[gl_state.currenttmu]=tile->texture;
 qglTexImage2D(GL_TEXTURE_2D,0,GL_RGB,texw,texh,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
 qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
 qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,SCALE_CLAMP_EDGE);
 qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,SCALE_CLAMP_EDGE);
 gen_fb(1,&scale_fb);
 if(!scale_fb) { ScaleRelease(); return false; }
 bind_fb(SCALE_FB,scale_fb);
 attach_tex(SCALE_FB,SCALE_COLOR,GL_TEXTURE_2D,tile->texture,0);
 if(depth_texture) {
  post_depth_texture=SCALE_TEX_BASE+SCALE_TILE_LIMIT;
  qglBindTexture(GL_TEXTURE_2D,post_depth_texture);
  gl_state.currenttextures[gl_state.currenttmu]=post_depth_texture;
  if(packed) qglTexImage2D(GL_TEXTURE_2D,0,0x88F0,texw,texh,0,0x84F9,0x84FA,NULL);	/* DEPTH24_STENCIL8, DEPTH_STENCIL, UNSIGNED_INT_24_8 */
  else qglTexImage2D(GL_TEXTURE_2D,0,SCALE_DEPTH24,texw,texh,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,SCALE_CLAMP_EDGE);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,SCALE_CLAMP_EDGE);
  attach_tex(SCALE_FB,SCALE_DEPTH,GL_TEXTURE_2D,post_depth_texture,0);
  if(packed) attach_tex(SCALE_FB,0x8D20,GL_TEXTURE_2D,post_depth_texture,0);	/* STENCIL_ATTACHMENT */
 } else {
  gen_rb(1,&scale_depth);
  if(!scale_depth) { ScaleRelease(); return false; }
  bind_rb(SCALE_RB,scale_depth);
  alloc_rb(SCALE_RB,packed?0x88F0:SCALE_DEPTH16,texw,texh);
  attach_rb(SCALE_FB,SCALE_DEPTH,SCALE_RB,scale_depth);
  if(packed) attach_rb(SCALE_FB,0x8D20,SCALE_RB,scale_depth);
  bind_rb(SCALE_RB,0);
 }
 if(check_fb(SCALE_FB)!=SCALE_COMPLETE || (qglGetError && qglGetError()!=GL_NO_ERROR)) {
  ScaleRelease();
  if(packed) {	/* 3.55ai: this driver has no packed depth + stencil for frame buffers: as before, without stencil */
   scale_packed_ok=0;
   return ScalePrepareFBO(width,height,post,depth_texture);
  }
  return false;
 }
 bind_fb(SCALE_FB,0);
 scale_width=width;
 scale_height=height;
 scale_backend=SCALE_FBO;
 return true;
}

qboolean GL_ScaleOffscreen(void)
{
 return scale_offscreen;
}

void GL_ScaleAbortFrame(void)
{
 post_frame=0;
 if(scale_offscreen) {
  bind_fb(SCALE_FB,0);
  scale_offscreen=false;
 }
}

static void ScaleCapture(int backend,int x,int y,int w,int h)
{
 if(backend==SCALE_COPY)
  qglCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,x,y,w,h);
 else {
  /* Tile widths are multiples of eight, so RGB rows already meet the
     default four-byte pack/unpack alignment even on MiniGL. */
  qglReadPixels(x,y,w,h,GL_RGB,GL_UNSIGNED_BYTE,scale_pixels);
  qglTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,scale_pixels);
 }
}

/* Some MiniGLs export a stub and also return GL_NO_ERROR. Verify the
   rendered colour through the real framebuffer once per allocation. */
static qboolean ScaleProbe(int backend,const refdef_t *view,float ui_scale)
{
 const gl_scale_tile_t *tile=&scale_tiles[0];
 float x=view->x/ui_scale,y=view->y/ui_scale,w=4/ui_scale;
 int bottom=vid.height-view->y-4;
 byte sample[48];

 GL_Bind(tile->texture);
 qglDisable(GL_TEXTURE_2D);
 qglColor4f(1,0,0,1);
 ScaleQuad(x,y,w,w,0,0,0,0);
 ScaleCapture(backend,view->x,bottom,4,4);
 qglColor4f(0,0,0,1);
 ScaleQuad(x,y,w,w,0,0,0,0);
 qglEnable(GL_TEXTURE_2D);
 qglColor4f(1,1,1,1);
 ScaleQuad(x,y,w,w,4*tile->u/tile->w,4*tile->v/tile->h,0,0);
 memset(sample,0,sizeof(sample));
 qglReadPixels(view->x,bottom,4,4,GL_RGB,GL_UNSIGNED_BYTE,sample);
 return sample[15]>180 && sample[16]<64 && sample[17]<64 &&
  (!qglGetError || qglGetError()==GL_NO_ERROR);
}

/* These sized RGB formats are available in OpenGL 1.1. In a 5/6/5
   framebuffer, RGB5 is a request for nearby precision. The driver may
   allocate 5/6/5 or a different format. */
static GLint ScaleCopyInternalFormat(qboolean match,const GLint rgb[3])
{
 if(!match) return gl_tex_solid_format;
 if(rgb[0]<=0 || rgb[1]<=0 || rgb[2]<=0) return 0;
 if(rgb[0]<=5 && rgb[1]<=6 && rgb[2]<=5) return GL_RGB5;
 return GL_RGB8;
}

static qboolean ScalePrepare(const refdef_t *view,int width,int height,float ui_scale,qboolean match)
{
 int limit=0,edge,x,y,texw,texh;
 unsigned used;
 gl_scale_tile_t *tile;

 ScaleRelease();
 copy_internal_format=0;
 if(!qglReadPixels || (!qglCopyTexSubImage2D && !qglTexSubImage2D)) return false;
 if(match && !qglCopyTexSubImage2D) return false;
 memset(copy_framebuffer_rgb,0,sizeof(copy_framebuffer_rgb));
 memset(copy_texture_rgb,0,sizeof(copy_texture_rgb));
 if(qglGetIntegerv) {
  qglGetIntegerv(GL_RED_BITS,&copy_framebuffer_rgb[0]);
  qglGetIntegerv(GL_GREEN_BITS,&copy_framebuffer_rgb[1]);
  qglGetIntegerv(GL_BLUE_BITS,&copy_framebuffer_rgb[2]);
 }
 copy_internal_format=ScaleCopyInternalFormat(match,copy_framebuffer_rgb);
 if(!copy_internal_format) return false;
 if(qglGetIntegerv) qglGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
 if(limit<=0) limit=256;
 for(edge=1;edge<=limit/2 && edge<2048;edge*=2) {}
 if(edge<64 || ((width+edge-1)/edge)*((height+edge-1)/edge)>SCALE_TILE_LIMIT) return false;
 texw=ScalePowerOfTwo(width<edge?width:edge);
 texh=ScalePowerOfTwo(height<edge?height:edge);
 if(texh<texw/8) texh=texw/8;
 if(texw<texh/8) texw=texh/8;
 scale_pixels=calloc(texw*texh,4);
 if(!scale_pixels) return false;
 used=(unsigned)texw*texh*4;

 ScaleClearErrors();
 GL_EnableMultitexture(false);
 for(y=0;y<height;y+=edge) for(x=0;x<width;x+=edge) {
  tile=&scale_tiles[scale_count];
  tile->x=x;
  tile->y=y;
  tile->w=width-x<edge?width-x:edge;
  tile->h=height-y<edge?height-y:edge;
  texw=ScalePowerOfTwo(tile->w);
  texh=ScalePowerOfTwo(tile->h);
  if(texh<texw/8) texh=texw/8;
  if(texw<texh/8) texw=texh/8;
  used+=(unsigned)texw*texh*4;
  if(used>SCALE_MEMORY_LIMIT) return false;
  tile->u=(float)tile->w/texw;
  tile->v=(float)tile->h/texh;
  tile->texture=SCALE_TEX_BASE+scale_count++;
  GL_Bind(tile->texture);
  /* The older 3dfx texture loaders need real initial pixel data. */
  qglTexImage2D(GL_TEXTURE_2D,0,copy_internal_format,texw,texh,0,GL_RGBA,GL_UNSIGNED_BYTE,scale_pixels);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 }
 if(qglGetError && qglGetError()!=GL_NO_ERROR) return false;
 if(qglReadBuffer)
  qglReadBuffer(!Q_stricmp(gl_drawbuffer->string,"GL_FRONT")?GL_FRONT:GL_BACK);
 if(qglCopyTexSubImage2D && ScaleProbe(SCALE_COPY,view,ui_scale)) {
  scale_backend=SCALE_COPY;
  free(scale_pixels);
  scale_pixels=NULL;
 } else {
  if(match) return false;
  ScaleClearErrors();
  if(!qglTexSubImage2D || !ScaleProbe(SCALE_READBACK,view,ui_scale)) return false;
  scale_backend=SCALE_READBACK;
 }
 if(qglGetTexLevelParameteriv && qglGetError) {
  /* ScaleProbe left the first copy texture bound. Query once, never during presentation. */
  qglGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_RED_SIZE,&copy_texture_rgb[0]);
  qglGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_GREEN_SIZE,&copy_texture_rgb[1]);
  qglGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_BLUE_SIZE,&copy_texture_rgb[2]);
  if(qglGetError()!=GL_NO_ERROR) {
   memset(copy_texture_rgb,0,sizeof(copy_texture_rgb));
   ScaleClearErrors();
  }
 }
 scale_width=width;
 scale_height=height;
 return true;
}

qboolean GL_ScaleBegin(const refdef_t *view,refdef_t *scaled_view,float ui_scale,const float *fog_color)
{
 int percent=RenderScalePercent(RenderScaleStep(scale_value?scale_value->value:100));
 int width,height,requested=0,target_request;
 qboolean copyformat=scale_copyformat->value && percent<100;
 qboolean pixelcopy=!copyformat && scale_pixelcopy->value && percent<100;

 if (scale_legacy_graphics) return false;

 post_frame=0;
 if(!copyformat && !pixelcopy) {
  if(post_fxaa->value) requested|=POST_FXAA;
  if(post_bloom->value) requested|=POST_BLOOM;
  if(post_ssao->value) requested|=POST_SSAO;
  if(post_tonemap->value>0) requested|=POST_TONEMAP;
  if(post_grade->value>0) requested|=POST_GRADE;
  if((post_available&POST_BRIGHTNESS) && fabs(SceneGamma()-1.0f)>0.001f) requested|=POST_BRIGHTNESS;
  if(percent<100 && scale_filter->value==3) requested|=POST_ADAPTIVE;
 }
 requested&=post_available;
 target_request=requested?((requested&POST_SSAO)?2:1):0;

 if(scale_legacy->modified || scale_pixelcopy->modified || scale_copyformat->modified) {
  ScaleRelease();
  rejected_width=rejected_height=0;
  scale_failed=false;
  ScaleUpdateCapability();
 }
 if((percent==100 && !requested) || scale_failed || gl_state.stereo_enabled || gl_nobind->value ||
  (view->rdflags&RDF_NOWORLDMODEL) || view->width<64 || view->height<64 ||
  RenderScaleWidth(vid.width,percent)<320 || RenderScaleHeight(vid.height,percent)<240) {
  if(percent==100 && scale_backend!=SCALE_NATIVE) ScaleRelease();
  PostReport(0);
  ScaleReport(vid.width,vid.height,SCALE_NATIVE);
  return false;
 }
 width=RenderScaleWidth(view->width,percent);
 height=RenderScaleHeight(view->height,percent);
 if(pixelcopy && !Q_stricmp(gl_drawbuffer->string,"GL_FRONT")) {
  ScaleFail();
  return false;
 }
 if(width==rejected_width && height==rejected_height && target_request==post_rejected_request) {
  PostReport(0);
  ScaleReport(vid.width,vid.height,SCALE_NATIVE);
  return false;
 }
 if(width!=scale_width || height!=scale_height || target_request!=post_target_request ||
  pixelcopy!=(scale_backend==SCALE_PIXEL_COPY) ||
  (pixelcopy && (pixel_view_x!=view->x || pixel_view_y!=view->y ||
   pixel_view_width!=view->width || pixel_view_height!=view->height))) {
  ScaleRelease();
  if(pixelcopy) {
   if(!ScalePreparePixelCopy(view,width,height,ui_scale)) {
    ri.Con_Printf(PRINT_ALL,"Direct pixel copy test failed or unavailable in this mode.\n");
    ScaleFail();
    return false;
   }
  } else if(copyformat ||
            (!ScalePrepareFBO(width,height,requested!=0,(requested&POST_SSAO)!=0) &&
             (!(requested&POST_SSAO) || !ScalePrepareFBO(width,height,true,false)))) {
   if(percent>=100 || (!copyformat && !scale_legacy->value)) {
    rejected_width=width;
    rejected_height=height;
    post_rejected_request=target_request;
    PostReport(0);
    ri.Con_Printf(PRINT_ALL,"OpenGL render scale needs a supported offscreen render target. Using native resolution.\n");
    ScaleReport(vid.width,vid.height,SCALE_NATIVE);
    return false;
   }
   if(!ScalePrepare(view,width,height,ui_scale,copyformat)) {
    if(copyformat)
     ri.Con_Printf(PRINT_ALL,"Copy texture format test failed (requested GL format 0x%X).\n",
      (unsigned)copy_internal_format);
    ScaleFail();
    return false;
   }
  }
  post_target_request=target_request;
 }
 if(scale_backend==SCALE_FBO) {
  post_frame=post_depth_texture?requested:requested&~POST_SSAO;
  if(percent==100 && !post_frame) {
   ScaleRelease();
   rejected_width=width;rejected_height=height;post_rejected_request=target_request;
   PostReport(0);
   ScaleReport(vid.width,vid.height,SCALE_NATIVE);
   return false;
  }
  bind_fb(SCALE_FB,scale_fb);
  scale_offscreen=true;
  qglClearDepth(gldepthmin<=gldepthmax?1:0);
  if(fog_color)
   qglClearColor(fog_color[0],fog_color[1],fog_color[2],1);
  qglClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  qglClearDepth(1);
  qglClearColor (0, 0, 0, 1);
 }
 *scaled_view=*view;
 scaled_view->width=width;
 scaled_view->height=height;
 ScaleReport(RenderScaleWidth(vid.width,percent),RenderScaleHeight(vid.height,percent),scale_backend);
 return true;
}

qboolean GL_ScalePresent(const refdef_t *scaled_view,const refdef_t *view,float ui_scale)
{
 int i;
 float scalex=(float)view->width/(scaled_view->width*ui_scale);
 float scaley=(float)view->height/(scaled_view->height*ui_scale);
 int bottom=vid.height-scaled_view->y-scaled_view->height;
 const gl_scale_tile_t *tile;

 int filter=scale_filter?(int)scale_filter->value:0,active=0;
 qboolean downsample=scaled_view->width>view->width;
 qboolean shader=false;
 int glfilter;
 PostReport(post_frame);
 GL_EnableMultitexture(false);
 if(scale_backend==SCALE_PIXEL_COPY) {
  qglReadBuffer(GL_BACK);
  if(!scale_checked) ScaleClearErrors();
  qglDisable(GL_TEXTURE_2D);
  qglDisable(GL_ALPHA_TEST);
  ScaleCopyPixels(view,scaled_view->width,scaled_view->height);
  qglEnable(GL_ALPHA_TEST);
  qglEnable(GL_TEXTURE_2D);
  if(!scale_checked && qglGetError()!=GL_NO_ERROR) {
   ScaleFail();
   return false;
  }
  scale_checked=1;
  if(reported_filter) {
   reported_filter=0;
   ri.Cvar_SetValue("gl_render_scale_filter_active",0);
  }
  return true;
 } else if(scale_backend==SCALE_FBO) {
  bind_fb(SCALE_FB,0);
  scale_offscreen=false;
 } else {
  if(qglReadBuffer)
   qglReadBuffer(!Q_stricmp(gl_drawbuffer->string,"GL_FRONT")?GL_FRONT:GL_BACK);
  if(!scale_checked) ScaleClearErrors();
  /* Copy every tile before any enlarged tile overwrites the source. */
  for(i=0;i<scale_count;i++) {
   tile=&scale_tiles[i];
   GL_Bind(tile->texture);
   ScaleCapture(scale_backend,scaled_view->x+tile->x,bottom+tile->y,tile->w,tile->h);
  }
  if(!scale_checked && qglGetError && qglGetError()!=GL_NO_ERROR) {
   ScaleFail();
   return false;
  }
  scale_checked=1;
 }
 if(!post_frame && filter==2 && !downsample && scale_count==1 && !scale_shader_checked) {
  scale_shader_checked=true;
  ScaleLoadShader();
 }
 shader=!post_frame && filter==2 && !downsample && scale_count==1 && scale_program!=0;
 if(post_frame&POST_ADAPTIVE) active=3;
 else if(scale_count==1 && (filter || downsample))
  active=(shader || (post_frame && filter==2 && !downsample))?2:1;
 if(reported_filter!=active) {
  reported_filter=active;
  ri.Cvar_SetValue("gl_render_scale_filter_active",reported_filter);
 }
 glfilter=(active || (post_frame&POST_FXAA))?GL_LINEAR:GL_NEAREST;
 qglEnable(GL_TEXTURE_2D);
 if(shader) use_program(scale_program);
 if(post_frame) {
  tile=&scale_tiles[0];
  use_program(post_program);
  uniform2f(post_step,tile->u/tile->w,tile->v/tile->h);
  uniform2f(post_end,tile->u,tile->v);
  uniform2f(post_effects,(post_frame&POST_FXAA)?1:0, (post_frame&POST_BLOOM)?(post_bloom->value>=2?.65f:.32f):0);
  uniform2f(post_colour,(post_frame&POST_TONEMAP)?1:0,
   (post_frame&POST_GRADE)?(post_grade->value>=2?2:1):0);
  if (post_brightness_loc>=0) uniform2f(post_brightness_loc,(post_frame&POST_BRIGHTNESS)?SceneGamma():1,0);
  uniform2f(post_ao_sharpen,(post_frame&POST_SSAO)?(post_ssao->value>=2?.85f:.5f):0,
   (post_frame&POST_ADAPTIVE)?2:active==2?1:0);
  if(post_frame&POST_SSAO) {
   post_active_texture(0x84C1);	/* GL_TEXTURE1 itself: the engine's GL_TEXTURE0 variable is 0 when gl_ext_multitexture is off (3.55ai) */
   qglBindTexture(GL_TEXTURE_2D,post_depth_texture);
   post_active_texture(0x84C0);
   post_uniform1i(post_sampler,1);
   uniform2f(post_depth_range,gldepthmin,1.0f/(gldepthmax-gldepthmin));
   uniform2f(post_ao_params,r_drawdistance>0?r_drawdistance+32:4096,scaled_view->height*10.0f);
  }
 }
 for(i=0;i<scale_count;i++) {
  float left=0,lower=0;
  tile=&scale_tiles[i];
  GL_Bind(tile->texture);
  if(scale_count==1) {
   if(texture_filter!=glfilter) {
    qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,glfilter);
    qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,glfilter);
    texture_filter=glfilter;
   }
   if(glfilter==GL_LINEAR && !shader && !post_frame) {
    left=tile->u/(2*tile->w);
    lower=tile->v/(2*tile->h);
   }
  }
  if(shader) {
   uniform2f(scale_shader_step,tile->u/tile->w,tile->v/tile->h);
   uniform2f(scale_shader_end,tile->u,tile->v);
  }
  ScaleQuad(view->x/ui_scale+tile->x*scalex,
   view->y/ui_scale+(scaled_view->height-tile->y-tile->h)*scaley,
   tile->w*scalex,tile->h*scaley,tile->u-left,tile->v-lower,left,lower);
 }
 if(shader || post_frame) use_program(0);
 if(post_frame&POST_SSAO) {
  post_active_texture(0x84C1);	/* GL_TEXTURE1 itself: the engine's GL_TEXTURE0 variable is 0 when gl_ext_multitexture is off (3.55ai) */
  qglBindTexture(GL_TEXTURE_2D,0);
  gl_state.currenttextures[1]=-1;
  post_active_texture(0x84C0);
 }
 return true;
}


/*
=================
BDGN 3.55t: heat distortion

Entities with RF_DISTORT are not drawn as models. After the translucent surfaces the 3D view is copied into a
texture once, and every distortion is drawn as a camera-facing disc that shows the copy, shifted by moving noise
that is strongest in the middle and zero at the edge (0 = haze over hot air, energy balls, muzzles) or on a ring
that runs outwards (1 = shock wave). The disc keeps the depth test, so walls in front hide it. GLSL only; the
capability is bdgn_gl_distort_capable (the client sends nothing otherwise).
=================
*/
#define DISTORT_TEXNUM (REFLECT_TEXNUM+1)
#define DISTORT_MAX 32
static GLuint distort_program;
static GLint distort_loc_tex,distort_loc_view,distort_loc_scale,distort_loc_time;
static void (APIENTRY *distort_uniform4f)(GLint,GLfloat,GLfloat,GLfloat,GLfloat);
static void (APIENTRY *distort_uniform1f)(GLint,GLfloat);
static void (APIENTRY *distort_uniform1i)(GLint,GLint);
static int distort_tw,distort_th,distort_count;
static entity_t *distort_list[DISTORT_MAX];

#define MAX_LAVA_HAZE_SURFACES 512
static msurface_t *lava_haze_surfaces[MAX_LAVA_HAZE_SURFACES];
static int lava_haze_count;
static cvar_t *distort_world;

void GL_InitDistortion(void)
{
#ifdef _WIN32
 static const char *fragment=
  "#version 110\n"
  "uniform sampler2D sceneTex; uniform vec4 view; uniform vec2 texScale; uniform float time;\n"
  "varying vec2 Q; varying vec4 P;\n"
  "void main(){float w; bool lava=P.z>.4&&P.z<.6&&P.y<.5;"
  "bool beam=P.z>.9&&P.y<.5; float r=beam?abs(Q.x):length(Q);"
  "if(!lava&&r>=1.0) discard;"
  "vec2 n=vec2(sin(Q.y*9.0+time*11.0+P.w*40.0)+sin(Q.x*13.0-time*7.0),"
  "cos(Q.x*8.0+time*10.0+P.w*40.0)+cos(Q.y*12.0+time*8.0));"
  "if(lava){w=.34+.19*sin(Q.x*.8+Q.y*.6+time*1.8);}"
  "else if(P.y<0.5){w=1.0-r*r; w*=w; if(beam) w*=2.8;}"
  "else{float d=abs(r-P.z); w=clamp(1.0-d*5.0,0.0,1.0); w*=w*(1.0-P.z); n+=Q/(r+0.001)*3.0;}"
  "vec2 px=gl_FragCoord.xy-view.xy+n*w*P.x*view.w*0.006;"
  "px=clamp(px,vec2(1.0),view.zw-vec2(1.0));"
  "gl_FragColor=vec4(texture2D(sceneTex,px*texScale).rgb,1.0);}";
 static const char *vertex=
  "#version 110\n"
  "varying vec2 Q; varying vec4 P;\n"
  "void main(){gl_Position=ftransform(); Q=gl_MultiTexCoord0.xy; P=gl_Color;}";
 distort_program=0;
 ri.Cvar_SetValue("bdgn_gl_distort_capable",0);
 if (!gl_soft_shadow_capable) return;	/* modern driver only: opengl32, OpenGL 2, SSE2, not legacy */
 SCALE_PROC(distort_uniform4f,"glUniform4f",NULL);
 SCALE_PROC(distort_uniform1f,"glUniform1f",NULL);
 SCALE_PROC(distort_uniform1i,"glUniform1i",NULL);
 if (!distort_uniform4f || !distort_uniform1f || !distort_uniform1i) return;
 distort_program=ScaleCompileShader(fragment,vertex);
 if (!distort_program) return;
 distort_loc_tex=uniform_location(distort_program,"sceneTex");
 distort_loc_view=uniform_location(distort_program,"view");
 distort_loc_scale=uniform_location(distort_program,"texScale");
 distort_loc_time=uniform_location(distort_program,"time");
 distort_tw=distort_th=0;
 distort_world=ri.Cvar_Get("cl_heatdistort","0",CVAR_ARCHIVE);
 ri.Cvar_SetValue("bdgn_gl_distort_capable",1);
#endif
}

void GL_DistortBeginFrame(void)
{
 water_view_ripple_count=-1;
 distort_count=0;
 lava_haze_count=0;
 weapon_haze_beams=haze_copy_failed=0;
}

qboolean GL_DistortWorldEnabled(void)
{
 return distort_program && distort_world && distort_world->value && !gl_reflect_pass;
}

void GL_DistortNoteLava(msurface_t *fa)
{
 if (!GL_DistortWorldEnabled() || lava_haze_count>=MAX_LAVA_HAZE_SURFACES ||
     !fa->polys || !fa->plane || fabs(fa->plane->normal[2])<0.7f ||
     !fa->texinfo || !fa->texinfo->image || !strstr(fa->texinfo->image->name,"lava"))
  return;
 if (r_origin[2]<=fa->polys->verts[0][2]+28) return;
 lava_haze_surfaces[lava_haze_count++]=fa;
}

/* R_DrawEntitiesOnList: true = a distortion entity (queued, never drawn as a model) */
qboolean GL_DistortQueue(entity_t *e)
{
 if (!(e->flags & RF_DISTORT)) return false;
 if (GL_DistortWorldEnabled() && !(e->flags&RF_DEPTHHACK) && distort_count<DISTORT_MAX && e->skinnum>0 && e->alpha>0)
  distort_list[distort_count++]=e;
 return true;
}

void GL_DrawDistortion(void)
{
#ifdef _WIN32
 GLint view[4];
 int i,j,tw,th,blend,alpha,cull;
 if (!distort_program || (!distort_count && !lava_haze_count)) return;
 qglGetIntegerv(GL_VIEWPORT,view);
 if (view[2]<8 || view[3]<8) { distort_count=0; return; }
 for (tw=64;tw<view[2];tw<<=1) ;
 for (th=64;th<view[3];th<<=1) ;
 GL_SelectTexture(GL_TEXTURE0);
 GL_Bind(DISTORT_TEXNUM);
 ScaleClearErrors();
 if (tw!=distort_tw || th!=distort_th) {
  qglTexImage2D(GL_TEXTURE_2D,0,GL_RGB,tw,th,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,SCALE_CLAMP_EDGE);
  qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,SCALE_CLAMP_EDGE);
  distort_tw=tw; distort_th=th;
  if (qglGetError()!=GL_NO_ERROR) { distort_tw=distort_th=0; distort_count=0; haze_copy_failed=1; return; }
 }
 qglCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,view[0],view[1],view[2],view[3]);
 if (qglGetError()!=GL_NO_ERROR) { haze_copy_failed=1; return; }
 cull=qglIsEnabled(GL_CULL_FACE);
 qglDisable(GL_CULL_FACE);
 blend=qglIsEnabled(GL_BLEND);
 alpha=qglIsEnabled(GL_ALPHA_TEST);
 qglDisable(GL_BLEND);
 qglDisable(GL_ALPHA_TEST);
 qglDepthMask(0);
 use_program(distort_program);
 distort_uniform1i(distort_loc_tex,0);
 distort_uniform4f(distort_loc_view,(float)view[0],(float)view[1],(float)view[2],(float)view[3]);
 uniform2f(distort_loc_scale,1.0f/tw,1.0f/th);
 distort_uniform1f(distort_loc_time,r_newrefdef.time);
 {	/* BDGN 3.56y: the haze sheet 28 units over the lava left a hard line where it ended (on the wall behind
	   the lava). Three weaker sheets above it (to 38 units, about 30 cm) fade it out: drawn from the top down, so
	   the strongest sheet wins wherever it covers the pixel and only the band above its edge gets the weaker ones. */
  static const float lift[4]={38,34.7f,31.3f,28}, power[4]={0.09f,0.2f,0.32f,0.45f};
  int layer;
  for (layer=0;layer<(lava_haze_count?4:0);layer++) {
   qglColor4f(power[layer],0,0.5f,0);
   for (i=0;i<lava_haze_count;i++) {
    glpoly_t *poly;
    for (poly=lava_haze_surfaces[i]->polys;poly;poly=poly->next) {
     qglBegin(GL_TRIANGLE_FAN);
     for (j=0;j<poly->numverts;j++) {
      float *v=poly->verts[j];
      qglTexCoord2f(v[0]*0.025f,v[1]*0.025f);
      qglVertex3f(v[0],v[1],v[2]+lift[layer]);
     }
     qglEnd();
    }
   }
  }
 }
 for (i=0;i<distort_count;i++) {
  entity_t *e=distort_list[i];
  float r=(float)e->skinnum,s=e->alpha>1?1:e->alpha;
  vec3_t R,U,q;
  int k;
  for (k=0;k<3;k++) { R[k]=vright[k]*r; U[k]=vup[k]*r; }
  if (e->flags & RF_DEPTHHACK) qglDepthRange(gldepthmin,gldepthmin+0.3*(gldepthmax-gldepthmin));
  if (e->frame==2 || e->frame==3) {	/* BDGN 3.55y: along a beam from origin to oldorigin, r = half width; haze across it */
   vec3_t d,mid,toeye,side;
   float length, end_radius=e->frame==3? e->oldframe/16.0f:r;
   VectorSubtract(e->oldorigin,e->origin,d);
   length=VectorNormalize(d);
   if (length>1) {
    if (e->frame==3) weapon_haze_beams++;
    for (k=0;k<3;k++) mid[k]=(e->origin[k]+e->oldorigin[k])*0.5f;
    VectorSubtract(r_origin,mid,toeye);
    CrossProduct(d,toeye,side);
    VectorNormalize(side);
    qglColor4f(s,0.0f,1.0f,(i*0.137f)-(int)(i*0.137f));
    qglBegin(GL_QUADS);
    for (k=0;k<3;k++) q[k]=e->origin[k]-side[k]*r;
    qglTexCoord2f(-1,0); qglVertex3fv(q);
    for (k=0;k<3;k++) q[k]=e->origin[k]+side[k]*r;
    qglTexCoord2f(1,0); qglVertex3fv(q);
    for (k=0;k<3;k++) q[k]=e->oldorigin[k]+side[k]*end_radius;
    qglTexCoord2f(1,length*0.025f); qglVertex3fv(q);
    for (k=0;k<3;k++) q[k]=e->oldorigin[k]-side[k]*end_radius;
    qglTexCoord2f(-1,length*0.025f); qglVertex3fv(q);
    qglEnd();
   }
   if (e->flags & RF_DEPTHHACK) qglDepthRange(gldepthmin,gldepthmax);
   continue;
  }
  qglColor4f(s,e->frame?1.0f:0.0f,e->oldframe*0.01f,(i*0.137f)-(int)(i*0.137f));
  qglBegin(GL_QUADS);
  for (k=0;k<3;k++) q[k]=e->origin[k]-R[k]-U[k];
  qglTexCoord2f(-1,-1); qglVertex3fv(q);
  for (k=0;k<3;k++) q[k]=e->origin[k]-R[k]+U[k];
  qglTexCoord2f(-1,1); qglVertex3fv(q);
  for (k=0;k<3;k++) q[k]=e->origin[k]+R[k]+U[k];
  qglTexCoord2f(1,1); qglVertex3fv(q);
  for (k=0;k<3;k++) q[k]=e->origin[k]+R[k]-U[k];
  qglTexCoord2f(1,-1); qglVertex3fv(q);
  qglEnd();
  if (e->flags & RF_DEPTHHACK) qglDepthRange(gldepthmin,gldepthmax);
 }
 use_program(0);
 qglDepthMask(1);
 qglColor4f(1,1,1,1);
 if (blend) qglEnable(GL_BLEND);
 if (alpha) qglEnable(GL_ALPHA_TEST);
 if (cull) qglEnable(GL_CULL_FACE);
 distort_count=0;
#endif
}

/*
BDGN 3.55ak: god rays (gl_godrays 1 soft, 2 strong) - shafts of light from the sun found in the sky (gl_sun.c).
The screen-space method most games of the 2000s used ("volumetric light scattering as a post-process"): everything
that is not sky is made black, and that picture is smeared from every pixel towards the sun's place on the screen
and added to the scene. The sky then shines past roofs, pillars and window frames in streaks that point away from
the sun, and what stands in front of the sun shuts them off.
Without a depth texture or a frame buffer object, so it runs in a window, full screen and inside the render-scale
target alike:
  1. the finished view is copied to a texture (to put it back afterwards)
  2. a black box a little smaller than the sky box is drawn around the eye with the depth test turned round
     (GL_GREATER): it lands on every pixel whose surface is nearer than the box - everything but the sky
  3. that masked picture is copied to a second texture and the view is put back from the first
  4. one shader pass adds the rays: 40 steps from the pixel towards the sun through the masked picture
Needs GLSL (bdgn_gl_godrays_capable). Not drawn when the sun is behind the viewer, in a map without a sun, with
gl_ztrick or with draw-distance fog. Two full-screen copies and one pass: for modern cards.
*/
#define GODRAY_TEXNUM (DISTORT_TEXNUM+1)	/* and +1 */
static GLuint godray_program;
static GLint godray_loc_tex,godray_loc_sun,godray_loc_scale,godray_loc_tint;
static void (APIENTRY *godray_uniform3f)(GLint,GLfloat,GLfloat,GLfloat);
static void (APIENTRY *godray_uniform1i)(GLint,GLint);
static int godray_tw,godray_th;
static cvar_t *gl_godrays;

static void GL_InitGodRays(void)
{
 static const char *source=
  "#version 110\n"
  "uniform sampler2D mask; uniform vec2 sun,scale; uniform vec3 tint;\n"
  "void main(){\n"
  "vec2 uv=gl_TexCoord[0].xy; vec2 d=(sun-uv)/40.0; vec2 p=uv;\n"
  "vec3 acc=vec3(0.0); float w=1.0;\n"
  "for(int i=0;i<40;i++){\n"
  " p+=d;\n"
  " vec3 c=texture2D(mask,p*scale).rgb;\n"
  " float l=max(c.r,max(c.g,c.b));\n"
  " float ok=step(0.0,p.x)*step(p.x,1.0)*step(0.0,p.y)*step(p.y,1.0);\n"
  " acc+=c*smoothstep(0.30,0.85,l)*w*ok;\n"
  " w*=0.96;\n"
  "}\n"
  "vec2 q=sun-uv; float fall=1.0/(1.0+dot(q,q)*2.5);\n"
  "gl_FragColor=vec4(acc*(1.0/40.0)*tint*fall,1.0);\n"
  "}\n";
 godray_program=0; godray_tw=godray_th=0;
 gl_godrays=ri.Cvar_Get("gl_godrays","0",CVAR_ARCHIVE);	/* 0 off, 1 soft, 2 strong */
 ri.Cvar_SetValue("bdgn_gl_godrays_capable",0);
 if (!gl_soft_shadow_capable) return;	/* modern driver only, as the heat distortion */
 SCALE_PROC(godray_uniform3f,"glUniform3f",NULL);
 SCALE_PROC(godray_uniform1i,"glUniform1i",NULL);
 if (!godray_uniform3f || !godray_uniform1i || !qglCopyTexSubImage2D || !qglGetIntegerv || !qglGetFloatv) return;
 godray_program=ScaleCompileShader(source,NULL);
 if (!godray_program) return;
 godray_loc_tex=uniform_location(godray_program,"mask");
 godray_loc_sun=uniform_location(godray_program,"sun");
 godray_loc_scale=uniform_location(godray_program,"scale");
 godray_loc_tint=uniform_location(godray_program,"tint");
 ri.Cvar_SetValue("bdgn_gl_godrays_capable",1);
}

static void GodRayQuad(float u,float v)
{
 qglBegin(GL_QUADS);
 qglTexCoord2f(0,0); qglVertex2f(-1,-1);
 qglTexCoord2f(u,0); qglVertex2f(1,-1);
 qglTexCoord2f(u,v); qglVertex2f(1,1);
 qglTexCoord2f(0,v); qglVertex2f(-1,1);
 qglEnd();
}

void GL_DrawGodRays(void)
{
#ifdef _WIN32
 static const float box[8][3]={{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
 static const int face[6][4]={{0,1,2,3},{4,5,6,7},{0,1,5,4},{2,3,7,6},{1,2,6,5},{0,3,7,4}};
 GLint view[4];
 GLfloat proj[16];
 float facing,eye[4],clip[4],p[3],su,sv,strength,us,vs;
 int i,k,tw,th,pass;
 if (!godray_program || !gl_godrays || !gl_godrays->value || !r_worldmodel || gl_reflect_pass ||
     (r_newrefdef.rdflags&RDF_NOWORLDMODEL) || gl_ztrick->value || r_drawdistance>0 || r_fullbright->value) return;
 R_SunCheck();
 if (!r_sun_valid) return;
 facing=DotProduct(r_sun_dir,vpn);
 if (facing<0.2f) return;	/* the sun is beside or behind the viewer */
 /* the sun's place on the screen */
 for (k=0;k<3;k++) p[k]=r_origin[k]+r_sun_dir[k]*1000.0f;
 for (k=0;k<4;k++) eye[k]=r_world_matrix[k]*p[0]+r_world_matrix[4+k]*p[1]+r_world_matrix[8+k]*p[2]+r_world_matrix[12+k];
 qglGetFloatv(GL_PROJECTION_MATRIX,proj);
 for (k=0;k<4;k++) clip[k]=proj[k]*eye[0]+proj[4+k]*eye[1]+proj[8+k]*eye[2]+proj[12+k]*eye[3];
 if (clip[3]<=1.0f) return;
 su=clip[0]/clip[3]*0.5f+0.5f; sv=clip[1]/clip[3]*0.5f+0.5f;
 if (su<-0.6f || su>1.6f || sv<-0.6f || sv>1.6f) return;	/* far outside the picture */
 qglGetIntegerv(GL_VIEWPORT,view);
 if (view[2]<64 || view[3]<64) return;
 for (tw=64;tw<view[2];tw<<=1) ;
 for (th=64;th<view[3];th<<=1) ;
 GL_SelectTexture(GL_TEXTURE0);
 if (tw!=godray_tw || th!=godray_th) {
  for (i=0;i<2;i++) {
   GL_Bind(GODRAY_TEXNUM+i);
   qglTexImage2D(GL_TEXTURE_2D,0,GL_RGB,tw,th,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
   qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
   qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
   qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,SCALE_CLAMP_EDGE);
   qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,SCALE_CLAMP_EDGE);
  }
  godray_tw=tw; godray_th=th;
  if (qglGetError()!=GL_NO_ERROR) { godray_tw=godray_th=0; return; }
 }
 us=(float)view[2]/tw; vs=(float)view[3]/th;
 /* 1: keep the view */
 GL_Bind(GODRAY_TEXNUM);
 qglCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,view[0],view[1],view[2],view[3]);
 /* 2: black on everything nearer than a box just inside the sky box (half size 2300 in gl_warp.c) */
 qglMatrixMode(GL_MODELVIEW);
 qglLoadMatrixf(r_world_matrix);
 qglDisable(GL_TEXTURE_2D);
 qglDisable(GL_ALPHA_TEST);
 qglDisable(GL_BLEND);
 qglDisable(GL_CULL_FACE);
 qglDepthMask(0);
 qglDepthFunc(GL_GREATER);
 qglColor4f(0,0,0,1);
 qglBegin(GL_QUADS);
 for (i=0;i<6;i++)
  for (k=0;k<4;k++)
   qglVertex3f(r_origin[0]+box[face[i][k]][0]*2240.0f,r_origin[1]+box[face[i][k]][1]*2240.0f,r_origin[2]+box[face[i][k]][2]*2240.0f);
 qglEnd();
 qglDepthFunc(GL_LEQUAL);
 qglEnable(GL_TEXTURE_2D);
 qglColor4f(1,1,1,1);
 /* 3: the masked picture, then the view back */
 GL_Bind(GODRAY_TEXNUM+1);
 qglCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,view[0],view[1],view[2],view[3]);
 qglDisable(GL_DEPTH_TEST);
 qglMatrixMode(GL_PROJECTION);
 qglPushMatrix();
 qglLoadIdentity();
 qglMatrixMode(GL_MODELVIEW);
 qglLoadIdentity();
 GL_TexEnv(GL_REPLACE);
 for (pass=0;pass<2;pass++) {
  if (!pass) {
   GL_Bind(GODRAY_TEXNUM);
   GodRayQuad(us,vs);
  } else {
   /* 4: the rays, added */
   strength=(gl_godrays->value>=2?1.15f:0.65f)*(facing-0.2f)/0.8f;
   if (strength>1.15f) strength=1.15f;
   GL_Bind(GODRAY_TEXNUM+1);
   qglEnable(GL_BLEND);
   qglBlendFunc(GL_ONE,GL_ONE);
   use_program(godray_program);
   godray_uniform1i(godray_loc_tex,0);
   uniform2f(godray_loc_sun,su,sv);
   uniform2f(godray_loc_scale,us,vs);
   godray_uniform3f(godray_loc_tint,(0.55f+0.45f*r_sun_color[0])*strength,(0.55f+0.45f*r_sun_color[1])*strength,(0.55f+0.45f*r_sun_color[2])*strength);
   GodRayQuad(1,1);
   use_program(0);
   qglBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
   qglDisable(GL_BLEND);
  }
 }
 qglMatrixMode(GL_PROJECTION);
 qglPopMatrix();
 qglMatrixMode(GL_MODELVIEW);
 qglLoadMatrixf(r_world_matrix);
 qglEnable(GL_DEPTH_TEST);
 qglDepthMask(1);
 qglEnable(GL_ALPHA_TEST);
 if (gl_cull->value) qglEnable(GL_CULL_FACE);
#endif
}
