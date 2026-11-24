#ifndef SND_WAVMUSIC_H
#define SND_WAVMUSIC_H

void WavMusic_Init (void);
qboolean WavMusic_IsIdle (void);
qboolean WavMusic_IsPlaying (void);
void WavMusic_Pump (void);
void WavMusic_Shutdown (void);
void WavMusic_Stop (void);
void WavMusic_SetVolume (float volume);
qboolean WavMusic_GetDirectory (char *path, int pathsize);
qboolean WavMusic_PlayTrack (int track, qboolean looping);
qboolean WavMusic_PlayFile (const char *filename, qboolean looping);
int WavMusic_ListFiles (char *files, int maxfiles, int stride);
qboolean WavMusic_PlaySelected (const char *filename, qboolean play_next);
void WavMusic_Update (void);
void WavMusic_SetPlayNext (qboolean play_next);
qboolean WavMusic_TrackExists (int track);
qboolean WavMusic_PlayTrackMP3 (int track, qboolean looping);
qboolean WavMusic_TrackMP3Exists (int track);

#endif
