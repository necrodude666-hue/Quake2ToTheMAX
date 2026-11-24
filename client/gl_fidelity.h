#ifndef BDGN_GL_FIDELITY_H
#define BDGN_GL_FIDELITY_H

typedef struct {
 const char *label;
 int scale,filter,fxaa,bloom,ssao,msaa,tonemap,grade;
} gl_visual_settings_t;

typedef struct {
 const char *name;
 int anisotropy;
 gl_visual_settings_t visual;
 int pixel;	/* BDGN 3.55i: High Fidelity tiers: gl_pixel_lighting (0 = not used, tier needs no shader lighting) */
 int hdset[4];	/* HD texture sets in order of preference (gl_hdtextures values), first installed one is used; 3.55o: AI first */
 int sound44;	/* 44 kHz HD sound when pak7-hd-sounds44.pak is installed */
} gl_fidelity_preset_t;

enum { GL_FIDELITY_FIRST=67 };
static const gl_fidelity_preset_t gl_fidelity_presets[]={
 {"ultrafidelity_msaa",4,{"Ultra Fidelity 1 - MSAA",100,0,0,0,0,4}},
 {"ultrafidelity_bloom",4,{"Ultra Fidelity 2 - Bloom",100,0,1,1,0,0,1,0}},
 {"ultrafidelity_ssao",8,{"Ultra Fidelity 3 - SSAO",100,0,1,2,2,0,1,1}},
 {"ultrafidelity_adaptive",8,{"Ultra Fidelity - Adaptive",75,3,1,1,1,0,1,1}}
};
#define GL_FIDELITY_COUNT (sizeof(gl_fidelity_presets)/sizeof(gl_fidelity_presets[0]))

/* BDGN 3.55i: High Fidelity 1-3, modern drivers only. Each step adds newer features: 1 = per-pixel light, relief,
   HD textures, FXAA, filmic, 44 kHz; 2 = 2 lights, 4x MSAA, bloom, SSAO low, grading; 3 = 4 lights, 8x MSAA, SSAO high.
   Listed only when the driver reports every feature of the step (VID_FidelityPresetAvailable). VSync stays off;
   3.55n: all three use shadows from the lights (gl_shadows 3) when the driver allows them. */
enum { GL_HIGHFI_FIRST=80 };
static const gl_fidelity_preset_t gl_highfi_presets[]={
 {"highfidelity1",8,{"High Fidelity 1 - Modern",100,0,1,0,0,0,1,0},1,{4,2,1,3},1},
 {"highfidelity2",16,{"High Fidelity 2 - Modern+",100,0,0,1,1,4,1,1},2,{4,3,1,2},1},
 {"highfidelity3",16,{"High Fidelity 3 - Maximum",100,3,1,2,1,8,1,1},4,{4,1,3,2},1},	/* 3.56a: ambient occlusion 1 (was 2), as in the owner's own config.cfg, the base of this preset */
 /* BDGN 3.55w: High Fidelity 3 with the AI 4K texture set (pak8-hd4k-*.pak, DDS); listed only when that set is
    installed and the driver takes compressed textures (vid_menu.c VID_FidelityPresetAvailable) */
 {"ultrahighfidelity4k",16,{"Ultra High Fidelity - 4K",100,0,1,2,0,8,1,1},4,{5,4,1,3},1}	/* 3.56v: the owner's config.cfg: bloom 2, no SSAO, 8x MSAA, colour grade */
};
#define GL_HIGHFI_COUNT (sizeof(gl_highfi_presets)/sizeof(gl_highfi_presets[0]))
#define GL_VISUAL_PRESET_COUNT (GL_FIDELITY_COUNT+GL_HIGHFI_COUNT)

static const gl_fidelity_preset_t *GL_FidelityPreset(int id)
{
 int index=id-GL_FIDELITY_FIRST;
 if (index>=0 && index<(int)GL_FIDELITY_COUNT) return &gl_fidelity_presets[index];
 index=id-GL_HIGHFI_FIRST;
 return index>=0 && index<(int)GL_HIGHFI_COUNT?&gl_highfi_presets[index]:NULL;
}

/* preset id of the n-th Ultra / High Fidelity tier (menu list and benchmark) */
static int GL_VisualPresetId(int n)
{
 return n<(int)GL_FIDELITY_COUNT?GL_FIDELITY_FIRST+n:GL_HIGHFI_FIRST+n-(int)GL_FIDELITY_COUNT;
}

static int GL_VisualEffects(const gl_visual_settings_t *v)
{
 return (v->fxaa?POST_FXAA:0)|(v->bloom?POST_BLOOM:0)|
  (v->ssao?POST_SSAO:0)|(v->filter==3?POST_ADAPTIVE:0)|
  (v->tonemap?POST_TONEMAP:0)|(v->grade?POST_GRADE:0);
}

#endif
