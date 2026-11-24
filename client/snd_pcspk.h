/* snd_pcspk.h -- BDGN 3.58g: "PC speaker" sound mode (win32/snd_pcspk.c) */
#ifndef SND_PCSPK_H
#define SND_PCSPK_H
int  PCSPK_Mode (void);	/* s_pcspeaker: 1 = PC speaker effects, no WAV mixed (automatic output), 2 = through the sound card */
void PCSPK_Paint (void *buffer, int count, int rate);	/* 3.58l: the mixer adds the effects (sound card output) */
void PCSPK_Sound (const char *name, qboolean own, float distance);
void PCSPK_Stop (void);
void PCSPK_Shutdown (void);
#endif
