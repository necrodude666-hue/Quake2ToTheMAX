#ifndef BDGN_SW_FEATURES_H
#define BDGN_SW_FEATURES_H
extern cvar_t *sw_drawflat_mode, *sw_fastworld, *sw_fastsky, *sw_fastwater, *sw_flatfloors;
extern cvar_t *sw_flatmodels, *sw_lowdetail, *sw_maxtexsize, *sw_brightskins;
extern cvar_t *sw_staticmap, *sw_dynamic, *sw_particles, *sw_scale;
extern cvar_t *sw_picmip;
void SW_BDGN_Init(void);
void SW_BDGN_Frame(void);
int SW_Fullbright(void);
float SW_LightScale(void);
const byte *SW_TintTable(void);		/* BDGN 3.52p: NULL unless sw_coloredlight is on */
const byte *SW_FullbrightTable(void);
extern float sw_drawdistance;	/* BDGN 3.52r */
void SW_FogScreen(void);
int SW_EntityBeyondDrawDistance(entity_t *e);
extern const byte *r_sprite_lightmap;	/* BDGN 3.52r */
const byte *R_SpriteLightMap (void);
extern const byte *sw_model_remap;	/* BDGN 3.52q: the current model's colour remap or NULL */
const byte *SW_ModelRemap(int tr,int tg,int tb);
void R_TintSurface(msurface_t *surf,int mip,byte *dest,int rowbytes,int width,int height,const int *lightadj);
int SW_NearestColor(int r,int g,int b);
int SW_ImageColor(image_t *image,int mode);
int SW_BrightColor(image_t *image);
int SW_BrightModel(const char *name,int flags);
int SW_AliasColor(image_t *skin);
void SW_PathsAnnounce(void);	/* BDGN 3.56c: sw_paths at map load */
#endif
