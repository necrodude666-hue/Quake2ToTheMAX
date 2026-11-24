#ifndef SND_MIDI_H
#define SND_MIDI_H
/* snd_midi.h -- BDGN 3.49h: MIDI sound effects and MIDI music (WinMM), implemented in win32/snd_midi.c */

#define MIDI_THEME_MENU			0
#define MIDI_THEME_BASE			1
#define MIDI_THEME_INDUSTRIAL	2
#define MIDI_THEME_TECH			3
#define MIDI_THEME_BOSS			4
#define MIDI_THEME_ACTION		5
#define MIDI_THEME_VICTORY		6
#define MIDI_NUMTHEMES			7

qboolean MIDI_IsIdle (void);
qboolean MIDI_IsPlaying (void);
qboolean MIDI_IsPlayingOn (int device);
int MIDI_DeviceCount (void);
qboolean MIDI_DeviceName (int device, char *name, int size);
int MIDI_HardwareDevice (void);	/* BDGN 3.58d: first MIDI output on the sound card (not a software synth), -2 = none */
void		MIDI_Init (void);
void		MIDI_Shutdown (void);
void MIDI_FinalShutdown (void);
void		MIDI_Update (void);						/* every frame: note offs, music */
int			MIDI_EffectsMode (void);				/* s_midieffects: 0 off, 1 mapped sounds as MIDI, 2 also drop all other sounds */
int			MIDI_MusicMode (void);					/* s_musicmode: 0 digital (CD), 1 retro MIDI, 2 off */
int			MIDI_NoSound (void);					/* s_nosound */
qboolean	MIDI_Maps (const char *name);			/* true if this sound is played as MIDI (and its WAV need not be loaded) */
qboolean	MIDI_PlayEffect (const char *name, int key, float volume);	/* true = played, do not mix the WAV */
void		MIDI_StopEffect (int key);
void		MIDI_StopEffects (void);
void		MIDI_PlayTheme (int theme);
void		MIDI_PlayMapMusic (int cdtrack);		/* picks the theme from the map name */
void		MIDI_MenuMusic (void);
void		MIDI_StopMusic (void);
void		MIDI_SetMusicVolume (float volume);

#endif
