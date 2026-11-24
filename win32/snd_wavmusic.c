/* WinMM music playback. PCM WAV and MP3 are streamed from disk (snd_musicstream.c) through a few small
   buffers that a worker thread refills, so a track is never loaded whole and plays on during level loads.
   MCI is the fallback: for WAV codecs other than PCM, and for MP3 the built-in decoder cannot read. */
#include <windows.h>
#include <mmsystem.h>
#include <process.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snd_musicstream.h"
#include "../client/client.h"
#include "snd_wavmusic.h"
#include "audio_cleanup.h"

#define MUSIC_BUFFERS	8	/* about 0.1 s each: under a second queued, so a volume change is heard soon */

static HWAVEOUT wav_device;
static WAVEHDR wav_headers[MUSIC_BUFFERS];
static qboolean wav_prepared[MUSIC_BUFFERS];
static qboolean wav_queued[MUSIC_BUFFERS];
static byte *wav_memory;
static int wav_buffer_bytes;
static musicstream_t wav_stream;			/* read only by the worker thread while it runs */
static HANDLE wav_event, wav_thread;
static volatile LONG wav_quit;
static volatile LONG wav_finished;			/* set by the worker: the stream ended and every buffer has played */
static volatile MMRESULT wav_thread_error;	/* set by the worker, printed by WavMusic_Update */
static const char * volatile wav_thread_call;
static volatile float wav_volume = 0.8f;
static char wav_filename[MAX_QPATH];
static qboolean mci_open;
static qboolean mci_loop;					/* MCI waveaudio has no "play ... repeat"; WavMusic_Update restarts it */
static qboolean mci_is_mp3;					/* MCI device is MPEGVideo (knows "repeat"), else waveaudio */
static char mci_alias[32] = "bdgnwav";
static cvar_t *music_volume;
static cvar_t *music_output; /* 0: shared game mixer (Win9x safe), 1: separate WinMM output */
static qboolean wav_shared;
static int wav_shared_bytes;
static char music_pending_file[MAX_QPATH];
static qboolean music_pending_loop;
static qboolean playlist_active;
static qboolean playlist_loop;
static int playlist_count, playlist_index;	/* playlist_count > 0: a soundtrack-list file is playing */
static char playlist_files[256][MAX_QPATH];

qboolean WavMusic_GetDirectory (char *path, int pathsize)
{
	char exe[MAX_OSPATH], basedir[MAX_OSPATH], candidate[MAX_OSPATH];
	char *slash, *leaf;
	DWORD length, attributes;

	if (!path || pathsize < 2) return false;
	length = GetModuleFileNameA (NULL, exe, sizeof(exe));
	if (length > 0 && length < sizeof(exe))
	{
		slash = strrchr (exe, '\\');
		if (!slash) slash = strrchr (exe, '/');
		if (slash)
		{
			*slash = 0;
			strncpy (basedir, exe, sizeof(basedir) - 1);
			basedir[sizeof(basedir) - 1] = 0;
			leaf = strrchr (basedir, '\\');
			if (!leaf) leaf = strrchr (basedir, '/');
			leaf = leaf ? leaf + 1 : basedir;
			if (!Q_stricmp (leaf, "baseq2"))
				Com_sprintf (candidate, sizeof(candidate), "%s/music", basedir);
			else
				Com_sprintf (candidate, sizeof(candidate), "%s/baseq2/music", basedir);
			attributes = GetFileAttributesA (candidate);
			if (attributes != 0xffffffff && (attributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				if ((int)strlen(candidate) >= pathsize) return false;
				strcpy (path, candidate);
				return true;
			}
		}
	}
	Com_sprintf (candidate, sizeof(candidate), "%s/music", FS_Gamedir ());
	attributes = GetFileAttributesA (candidate);
	if (attributes != 0xffffffff && (attributes & FILE_ATTRIBUTE_DIRECTORY) && (int)strlen(candidate) < pathsize)
	{
		strcpy (path, candidate);
		return true;
	}
	path[0] = 0;
	return false;
}

static qboolean Music_Path (const char *filename, char *path, int pathsize)
{
	char musicdir[MAX_OSPATH];
	int i;
	if (!filename || !filename[0] || strstr (filename, "..") || strchr (filename, ':') || strchr (filename, '\\') || strchr (filename, '"'))
		return false;
	if (strchr (filename, '/') || !WavMusic_GetDirectory (musicdir, sizeof(musicdir))) return false;
	Com_sprintf (path, pathsize, "%s/%s", musicdir, filename);
	for (i = 0 ; path[i] ; i++) if (path[i] == '/') path[i] = '\\';
	return true;
}

static void Music_ReportWaveError (const char *filename, const char *call, MMRESULT result)
{
	char text[MAXERRORLENGTH];
	if (waveOutGetErrorText (result, text, sizeof(text)) != MMSYSERR_NOERROR)
		strcpy (text, "unknown error");
	Com_Printf ("WavMusic: could not play %s: %s failed (%u): %s\n", filename, call, (unsigned)result, text);
}

/* Refills and queues up to max_new of the buffers the device has finished with; returns how many are queued.
   Run by the worker thread, and once by Music_PlayStream before the thread starts.
   On a WinMM error it stops reading and returns the error in *error and the call's name in *call. */
static int Music_FillBuffers (MMRESULT *error, const char **call, int max_new)
{
	int i, bytes, queued = 0, added = 0;
	MMRESULT result;
	WAVEHDR *header;

	for (i = 0 ; i < MUSIC_BUFFERS ; i++)
	{
		header = &wav_headers[i];
		if (wav_queued[i] && !(header->dwFlags & WHDR_DONE))
		{
			queued++;
			continue;
		}
		wav_queued[i] = false;
		if (wav_prepared[i])
		{
			result = waveOutUnprepareHeader (wav_device, header, sizeof(WAVEHDR));
			if (result != MMSYSERR_NOERROR)
			{	/* WinMM still owns it: never refill this buffer */
				if (*error == MMSYSERR_NOERROR)
				{
					*error = result;
					*call = "waveOutUnprepareHeader";
				}
				continue;
			}
			wav_prepared[i] = false;
		}
		if (wav_quit || wav_stream.ended || *error != MMSYSERR_NOERROR || added >= max_new)
			continue;
		bytes = MusicStream_Read (&wav_stream, wav_memory + i * wav_buffer_bytes, wav_buffer_bytes, wav_volume);
		if (bytes <= 0)
			continue;
		memset (header, 0, sizeof(WAVEHDR));
		header->lpData = (LPSTR)(wav_memory + i * wav_buffer_bytes);
		header->dwBufferLength = (DWORD)bytes;
		result = waveOutPrepareHeader (wav_device, header, sizeof(WAVEHDR));
		if (result != MMSYSERR_NOERROR)
		{
			*error = result;
			*call = "waveOutPrepareHeader";
			continue;
		}
		wav_prepared[i] = true;
		result = waveOutWrite (wav_device, header, sizeof(WAVEHDR));
		if (result != MMSYSERR_NOERROR)
		{
			*error = result;
			*call = "waveOutWrite";
			continue;
		}
		wav_queued[i] = true;
		queued++;
		added++;
	}
	return queued;
}

/* Worker: wakes when the device finishes a buffer (CALLBACK_EVENT), refills it, and exits when told to
   or when everything has played. It makes no Com_ calls; the main thread reports its errors. */
static unsigned __stdcall Music_Thread (void *unused)
{
	MMRESULT error = MMSYSERR_NOERROR;
	const char *call = NULL;
	while (!wav_quit)
	{
		if (!Music_FillBuffers (&error, &call, MUSIC_BUFFERS) && (wav_stream.ended || error != MMSYSERR_NOERROR))
		{
			wav_finished = 1;
			break;
		}
		if (error != MMSYSERR_NOERROR && !wav_thread_error)
		{
			wav_thread_call = call;
			wav_thread_error = error;
		}
		WaitForSingleObject (wav_event, 250);
	}
	if (error != MMSYSERR_NOERROR && !wav_thread_error)
	{
		wav_thread_call = call;
		wav_thread_error = error;
	}
	return 0;
}

static qboolean Music_StopWave (void)
{
	int i;
	if (wav_thread)
	{
		wav_quit = 1;
		if (wav_event)
			SetEvent (wav_event);
		if (WaitForSingleObject (wav_thread, 1000) != WAIT_OBJECT_0)
		{
			Com_Printf ("WavMusic: worker has not stopped; retaining thread, device and buffers\n");
			return false;
		}
		CloseHandle (wav_thread);
		wav_thread = NULL;
	}
	if (wav_device)
	{
		if (!Audio_CloseWave (&wav_device, wav_headers, MUSIC_BUFFERS, "WavMusic"))
			return false;
	}
	for (i = 0 ; i < MUSIC_BUFFERS ; i++)
		wav_prepared[i] = wav_queued[i] = false;
	if (wav_event)
	{
		CloseHandle (wav_event);
		wav_event = NULL;
	}
	MusicStream_Close (&wav_stream);
	if (wav_memory)
		free (wav_memory);
	wav_memory = NULL;
	wav_finished = 0;
	wav_thread_error = MMSYSERR_NOERROR;	/* belongs to the track being stopped */
	if (wav_shared) S_ClearMusicSamples ();
	wav_shared = false;
	return true;
}

void WavMusic_Stop (void)
{
	music_pending_file[0] = 0;
	playlist_active = false;
	playlist_count = 0;
	Music_StopWave ();
	if (mci_open)
	{
		DWORD error;
		mciSendString ("stop bdgnwav wait", NULL, 0, NULL);
		error = mciSendString ("close bdgnwav wait", NULL, 0, NULL);
		if (error) Com_Printf ("WavMusic: MCI close failed (%lu); retaining alias\n", error);
		else mci_open = false;
	}
	mci_loop = false;
}

void WavMusic_SetVolume (float volume)
{
	char command[80];
	int value;
	if (volume < 0) volume = 0;
	if (volume > 1) volume = 1;
	if (music_volume) Cvar_SetValue ("s_musicvolume", volume);
	wav_volume = volume;	/* the worker scales every buffer it fills from now on */
	if (mci_open)
	{
		value = (int)(volume * 1000.0f);
		Com_sprintf (command, sizeof(command), "setaudio %s volume to %d", mci_alias, value);
		mciSendString (command, NULL, 0, NULL);
	}
}

/* Streams a PCM WAV or an MP3 through WinMM. Failures in WinMM are printed here; a stream that cannot be
   opened is returned in *stream_error for the caller to report or hand to MCI. */
static qboolean Music_PlayStream (const char *filename, const char *path, qboolean is_mp3, qboolean looping, musicstream_error_t *stream_error)
{
	WAVEFORMATEX format;
	MMRESULT result;
	MMRESULT fill_error = MMSYSERR_NOERROR;
	const char *fill_call = NULL;
	unsigned thread_id;

	*stream_error = MUSICSTREAM_OK;
	if (!Music_StopWave ()) return false;
	*stream_error = is_mp3 ? MusicStream_OpenMP3 (&wav_stream, path, looping) : MusicStream_OpenWav (&wav_stream, path, looping);
	if (*stream_error != MUSICSTREAM_OK)
		return false;
	memset (&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = (WORD)wav_stream.channels;
	format.nSamplesPerSec = (DWORD)wav_stream.rate;
	format.wBitsPerSample = (WORD)wav_stream.bits;
	format.nBlockAlign = (WORD)wav_stream.align;
	format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
	wav_buffer_bytes = (int)(format.nAvgBytesPerSec / 10);
	if (wav_buffer_bytes < 4096)
		wav_buffer_bytes = 4096;
	wav_buffer_bytes -= wav_buffer_bytes % wav_stream.align;
	if (!music_output || !music_output->value)
	{
		if (!S_MusicOutputReady ())
		{
			Com_Printf ("WavMusic: game sound output unavailable; initialize sound before music\n");
			Music_StopWave ();
			return false;
		}
		wav_memory = (byte *)malloc (wav_buffer_bytes);
		if (!wav_memory) { Music_StopWave (); return false; }
		wav_shared = true;
		wav_shared_bytes = 0;
		wav_volume = music_volume ? music_volume->value : 1.0f;
		Com_DPrintf ("WavMusic: streaming %s through shared game mixer\n", filename);
		return true;
	}
	wav_memory = (byte *)malloc (wav_buffer_bytes * MUSIC_BUFFERS);
	wav_event = CreateEvent (NULL, FALSE, FALSE, NULL);
	if (!wav_memory || !wav_event)
	{
		Com_Printf ("WavMusic: could not play %s: out of memory\n", filename);
		Music_StopWave ();
		return false;
	}
	result = waveOutOpen (&wav_device, WAVE_MAPPER, &format, (DWORD_PTR)wav_event, 0, CALLBACK_EVENT);
	if (result != MMSYSERR_NOERROR)
	{
		wav_device = NULL;
		Music_ReportWaveError (filename, "waveOutOpen", result);
		Music_StopWave ();
		return false;
	}
	strncpy (wav_filename, filename, sizeof(wav_filename) - 1);
	wav_filename[sizeof(wav_filename) - 1] = 0;
	wav_quit = 0;
	wav_finished = 0;
	wav_thread_error = MMSYSERR_NOERROR;
	wav_volume = music_volume ? music_volume->value : 0.8f;
	if (wav_volume < 0) wav_volume = 0;
	if (wav_volume > 1) wav_volume = 1;
	/* queue two buffers here so WinMM errors are reported now; the worker reads the rest, so starting a
	   track does not hold up the game while 0.8 s of MP3 is decoded */
	Music_FillBuffers (&fill_error, &fill_call, 2);
	if (fill_error != MMSYSERR_NOERROR)
	{
		Music_ReportWaveError (filename, fill_call, fill_error);
		Music_StopWave ();
		return false;
	}
	wav_thread = (HANDLE)_beginthreadex (NULL, 0, Music_Thread, NULL, 0, &thread_id);
	if (!wav_thread)
	{
		Com_Printf ("WavMusic: could not play %s: could not start the music thread\n", filename);
		Music_StopWave ();
		return false;
	}
	SetThreadPriority (wav_thread, THREAD_PRIORITY_ABOVE_NORMAL);
	Com_DPrintf ("WavMusic: streaming %s (%d Hz, %d-bit, %d ch)\n", filename, wav_stream.rate, wav_stream.bits, wav_stream.channels);
	return true;
}

qboolean WavMusic_PlayFile (const char *filename, qboolean looping)
{
	char path[MAX_OSPATH], command[MAX_OSPATH + 100], error_text[256];
	DWORD error;
	musicstream_error_t stream_error;
	const char *ext = strrchr (filename ? filename : "", '.');
	const char *type;
	qboolean is_mp3;
	if (!filename || !ext) return false;
	CDAudio_Stop (); /* release all previous music backends before acquiring one */
	if (!CDAudio_ReleaseDevice () || !WavMusic_IsIdle ()) return false;
	if (!MIDI_IsIdle ())
	{
		/* An asynchronous MIDI open is being cancelled. Defer acquisition; never wait on the game thread. */
		Com_sprintf (music_pending_file, sizeof(music_pending_file), "%s", filename);
		music_pending_loop = looping;
		return true;
	}
	if (!Q_stricmp ((char *)ext, ".wav"))
		is_mp3 = false;
	else if (!Q_stricmp ((char *)ext, ".mp3"))
		is_mp3 = true;
	else
		return false;
	if (!Music_Path (filename, path, sizeof(path)))
		return false;
	if (Music_PlayStream (filename, path, is_mp3, looping, &stream_error))
		return true;
	/* WinMM errors were printed; a missing file or a broken WAV is not worth an MCI attempt */
	if (stream_error == MUSICSTREAM_OK || stream_error == MUSICSTREAM_ERR_OPEN || stream_error == MUSICSTREAM_ERR_FORMAT)
	{
		if (stream_error != MUSICSTREAM_OK)
			Com_Printf ("WavMusic: could not play %s: %s\n", filename, MusicStream_ErrorText (stream_error));
		return false;
	}
	if (stream_error != MUSICSTREAM_OK)
		Com_DPrintf ("WavMusic: %s: %s; trying MCI\n", filename, MusicStream_ErrorText (stream_error));
	type = is_mp3 ? "MPEGVideo" : "waveaudio";
	Com_sprintf (command, sizeof(command), "open \"%s\" type %s alias %s", path, type, mci_alias);
	error = mciSendString (command, NULL, 0, NULL);
	if (error)
	{
		if (!mciGetErrorString (error, error_text, sizeof(error_text))) strcpy (error_text, "unknown MCI error");
		Com_Printf ("WavMusic: could not open %s: %s\n", filename, error_text);
		return false;
	}
	mci_open = true;
	WavMusic_SetVolume (music_volume ? music_volume->value : 0.8f);
	/* only MPEGVideo knows "repeat"; waveaudio rejects it (MCI error 259), so WavMusic_Update loops it */
	Com_sprintf (command, sizeof(command), "play %s%s", mci_alias, looping && is_mp3 ? " repeat" : "");
	error = mciSendString (command, NULL, 0, NULL);
	if (error)
	{
		if (!mciGetErrorString (error, error_text, sizeof(error_text))) strcpy (error_text, "unknown MCI error");
		Com_Printf ("WavMusic: could not play %s: %s\n", filename, error_text);
		WavMusic_Stop ();
		return false;
	}
	mci_loop = looping && !is_mp3;
	mci_is_mp3 = is_mp3;
	return true;
}

static qboolean Music_TrackExists (int track, const char *extension)
{
	char filename[32], path[MAX_OSPATH];
	FILE *f;
	if (track < 1 || track > 99) return false;
	Com_sprintf (filename, sizeof(filename), "track%02d.%s", track, extension);
	if (!Music_Path (filename, path, sizeof(path))) return false;
	f = fopen (path, "rb");
	if (!f) return false;
	fclose (f);
	return true;
}

/* Numbered WAV files are the game's built-in CD-track replacements, not user playlist entries. */
static qboolean Music_IsDefaultWavTrack (const char *filename)
{
	if (!filename || strlen (filename) != 11 || tolower ((unsigned char)filename[0]) != 't' ||
		tolower ((unsigned char)filename[1]) != 'r' || tolower ((unsigned char)filename[2]) != 'a' ||
		tolower ((unsigned char)filename[3]) != 'c' || tolower ((unsigned char)filename[4]) != 'k')
		return false;
	if (filename[5] < '0' || filename[5] > '9' || filename[6] < '0' || filename[6] > '9')
		return false;
	return Q_stricmp ((char *)filename + 7, ".wav") == 0;
}

int WavMusic_ListFiles (char *files, int maxfiles, int stride)
{
	char musicdir[MAX_OSPATH], pattern[MAX_OSPATH], found[256][MAX_QPATH], temp[MAX_QPATH];
	WIN32_FIND_DATAA data; HANDLE search; int count=0, i, j, limit;
	if (!files || maxfiles <= 0 || stride < MAX_QPATH || !WavMusic_GetDirectory(musicdir,sizeof(musicdir))) return 0;
	limit = maxfiles < 256 ? maxfiles : 256;
	Com_sprintf(pattern,sizeof(pattern),"%s\\*.*",musicdir);
	search=FindFirstFileA(pattern,&data); if(search==INVALID_HANDLE_VALUE) return 0;
	do { const char *ext=strrchr(data.cFileName,'.'); if((data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||!ext||Music_IsDefaultWavTrack(data.cFileName)||(Q_stricmp((char *)ext,".wav")&&Q_stricmp((char *)ext,".mp3"))) continue; if(count<limit){strncpy(found[count],data.cFileName,MAX_QPATH-1);found[count][MAX_QPATH-1]=0;count++;}} while(FindNextFileA(search,&data));
	FindClose(search);
	for(i=1;i<count;i++){strcpy(temp,found[i]);j=i;while(j>0&&Q_stricmp(found[j-1],temp)>0){strcpy(found[j],found[j-1]);j--;}strcpy(found[j],temp);}
	for(i=0;i<count;i++)strcpy(files+i*stride,found[i]);
	return count;
}

qboolean WavMusic_PlaySelected (const char *filename, qboolean play_next)
{
	char musicdir[MAX_OSPATH], pattern[MAX_OSPATH], temp[MAX_QPATH];
	WIN32_FIND_DATAA data; HANDLE find; int i, j, count=0, selected=-1;
	if (!filename || !WavMusic_GetDirectory (musicdir, sizeof(musicdir))) return false;
	Com_sprintf (pattern, sizeof(pattern), "%s\\*.*", musicdir);
	find = FindFirstFileA (pattern, &data);
	if (find == INVALID_HANDLE_VALUE) return false;
	do { const char *ext=strrchr(data.cFileName,'.'); if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !ext || Music_IsDefaultWavTrack(data.cFileName) || (Q_stricmp((char *)ext,".wav") && Q_stricmp((char *)ext,".mp3"))) continue; if (count<256) { strncpy(playlist_files[count],data.cFileName,sizeof(playlist_files[count])-1); playlist_files[count][sizeof(playlist_files[count])-1]=0; count++; } } while (FindNextFileA(find,&data));
	FindClose(find);
	for(i=1;i<count;i++){ strcpy(temp,playlist_files[i]); j=i; while(j>0 && Q_stricmp(playlist_files[j-1],temp)>0){ strcpy(playlist_files[j],playlist_files[j-1]); j--; } strcpy(playlist_files[j],temp); }
	for(i=0;i<count;i++) if(!Q_stricmp(playlist_files[i],filename)){selected=i;break;}
	if(selected<0) return WavMusic_PlayFile(filename,!play_next);
	if(!WavMusic_PlayFile(playlist_files[selected],!play_next)) return false;
	playlist_count=count; playlist_index=selected; playlist_loop=!play_next; playlist_active=play_next; return true;
}

qboolean WavMusic_IsPlaying (void) { return wav_device || wav_shared || mci_open; }

qboolean WavMusic_IsIdle (void)
{
 return !wav_device && !wav_shared && !mci_open && !wav_thread;
}

/* Called just before mixing, with exclusive access to the stream and sample ring. */
void WavMusic_Pump (void)
{
 int frames, bytes;
 if (!wav_shared) return;
 frames = S_MusicSamplesNeeded (wav_stream.rate);
 if (frames <= 0) return;
 bytes = frames * wav_stream.align;
 if (bytes > wav_buffer_bytes) bytes = wav_buffer_bytes;
 bytes -= bytes % wav_stream.align;
 bytes = MusicStream_Read (&wav_stream, wav_memory, bytes, wav_volume);
 wav_shared_bytes = bytes;
 if (bytes > 0)
  S_RawSamples (bytes / wav_stream.align, wav_stream.rate, wav_stream.bits / 8, wav_stream.channels, wav_memory);
}

void WavMusic_Update (void)
{
	qboolean finished=false; char mode[32];
	if (music_pending_file[0])
	{
		char pending[MAX_QPATH];
		int count, index;
		qboolean loop, active, pending_loop;
		if (!MIDI_IsIdle ()) return;
		strcpy (pending, music_pending_file);
		pending_loop = music_pending_loop;
		count = playlist_count; index = playlist_index; loop = playlist_loop; active = playlist_active;
		if (WavMusic_PlayFile (pending, pending_loop))
		{
			playlist_count = count; playlist_index = index; playlist_loop = loop; playlist_active = active;
		}
		return;
	}
	if (wav_thread_error != MMSYSERR_NOERROR)
	{
		Music_ReportWaveError (wav_filename, wav_thread_call ? wav_thread_call : "waveOut", wav_thread_error);
		wav_thread_error = MMSYSERR_NOERROR;
	}
	if(mci_open && mciSendString("status bdgnwav mode",mode,sizeof(mode),NULL)==0 && !Q_stricmp(mode,"stopped"))
	{
		if(mci_loop){ if(mciSendString("play bdgnwav from 0",NULL,0,NULL)) mci_loop=false; return; }
		finished=true;
	}
	if(wav_device && wav_finished) finished=true;
	if(wav_shared && wav_stream.ended && !wav_shared_bytes && S_MusicSamplesPending () == 0) finished=true;
	if(!playlist_active) { if(finished) WavMusic_Stop (); return; }
	if(!finished) return;
	/* next file, wrapping to the first; a file that cannot be played is skipped (it has printed why) */
	{
		int count=playlist_count, index=playlist_index, tries;
		qboolean loop=playlist_loop;
		char next[MAX_QPATH];
		for (tries = 0; tries < count; tries++)
		{
			index = (index + 1) % count;
			strcpy (next, playlist_files[index]);
			if (WavMusic_PlayFile (next, loop))
			{
				playlist_count=count; playlist_index=index; playlist_loop=loop; playlist_active=true;
				Cvar_Set ("s_musicfile", next);	/* the menu and the next map follow the playing file */
				return;
			}
		}
	}
}

/* The soundtrack menu's playback toggle, applied to the file that is playing now (not only the next one). */
void WavMusic_SetPlayNext (qboolean play_next)
{
	char command[64], file[MAX_QPATH];
	int count, index;
	if (playlist_count <= 0)
		return;	/* nothing from the soundtrack list is playing */
	playlist_loop = !play_next;
	playlist_active = play_next;
	if (wav_device || wav_shared)
	{
		/* the worker reads looping when the file ends; the interlocked write orders it before the read of
		   ended below, so a file the worker has already finished reading is never missed */
		InterlockedExchange ((LONG volatile *)&wav_stream.looping, play_next ? 0 : 1);
		if (!play_next && wav_stream.ended)
		{	/* switched to "repeat track" after the whole file was read: start it again */
			count = playlist_count;
			index = playlist_index;
			strcpy (file, playlist_files[index]);
			if (WavMusic_PlayFile (file, true))
			{
				playlist_count = count;
				playlist_index = index;
				playlist_loop = true;
			}
		}
	}
	else if (mci_open && mci_is_mp3)
	{	/* "play" without "from" continues from the current position */
		Com_sprintf (command, sizeof(command), "play %s%s", mci_alias, play_next ? "" : " repeat");
		mciSendString (command, NULL, 0, NULL);
	}
	else if (mci_open)
		mci_loop = !play_next;
}

qboolean WavMusic_TrackExists (int track) { return Music_TrackExists (track, "wav"); }
qboolean WavMusic_TrackMP3Exists (int track) { return Music_TrackExists (track, "mp3"); }

qboolean WavMusic_PlayTrack (int track, qboolean looping)
{
	char filename[32];
	if (track < 1 || track > 99) return false;
	Com_sprintf (filename, sizeof(filename), "track%02d.wav", track);
	return WavMusic_PlayFile (filename, looping);
}

qboolean WavMusic_PlayTrackMP3 (int track, qboolean looping)
{
	char filename[32];
	if (track < 1 || track > 99) return false;
	Com_sprintf (filename, sizeof(filename), "track%02d.mp3", track);
	return WavMusic_PlayFile (filename, looping);
}

void WavMusic_Init (void)
{
	music_volume = Cvar_Get ("s_musicvolume", "1.0", CVAR_ARCHIVE);
	music_output = Cvar_Get ("s_musicoutput", "0", CVAR_ARCHIVE);
	Cvar_Get ("s_musicsource", "1", CVAR_ARCHIVE); /* Standard uses PCM WAV for numbered game tracks. */
	Cvar_Get ("s_musicdefault", "1", CVAR_ARCHIVE);
}

void WavMusic_Shutdown (void)
{
	WavMusic_Stop ();
}
