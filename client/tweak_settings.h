/* One shared catalogue for the two tweak menus and benchmark. VS2005 C. */
#ifndef BDGN_TWEAK_SETTINGS_H
#define BDGN_TWEAK_SETTINGS_H
#define TK_GL 1
#define TK_RESTART 2
#define TK_3DNOW 4
#define TK_SSE 8
#define TK_MMX 16
#define TK_SERVER 32
#define TK_MENUONLY 64	/* BDGN 3.55i: picture choice shown in a tweak page; not a benchmark case, not in presets/defaults */
#define TK_MAX 160	/* 3.59f: 160 (130 in use); 3.59o: one more */ /* BDGN 3.55t: room for more rows; 3.57a: 128 (98 in use); 3.57c: 101 in use; 3.57d: 106 */
typedef struct {
 const char *category, *label, *cvar, *aux, *default_value;
 int menu, count, values[12], baseline, on, speed, flags, kind;	/* BDGN 3.56r: 12 (was 8), 3D railgun colours has 10 */
 const char *names[13];
} bdgn_tweak_t;
extern const bdgn_tweak_t bdgn_tweaks[];
extern const int bdgn_tweak_count;
int BDGN_RendererSupports (const char *name);
int BDGN_TweakAllowed (int index, int choice);
int BDGN_TweakRead (int index);
void BDGN_TweakDefaults (void);
void BDGN_SyncEnemyLook (void);	/* BDGN 3.52f */
void BDGN_TweakSet (int index, int choice);
void M_Menu_TweaksGraphics_f (void);
void M_Menu_TweaksEffects_f (void);
void M_Menu_TweaksLighting_f (void);	/* BDGN 3.55i */
void M_Menu_TweaksEnemies_f (void);
void M_Menu_TweaksPaths_f (void);
void M_Menu_TweaksTesting_f (void);
void M_Menu_TweaksSimpler_f (void);	/* BDGN 3.57d */
void M_Menu_TweaksOldSchool_f (void);
int BDGN_TweakVisible (int index);
void BDGN_RetroModeSet (int mode);	/* BDGN 3.58w: 0 off, 1 Doom, 2 Wolfenstein 3D, 3 Wolfenstein 3D low-res */
#endif
