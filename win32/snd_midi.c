/*
 * snd_midi.c -- BDGN 3.49h: MIDI sound effects and MIDI music through WinMM (midiOutShortMsg).
 *
 * s_midieffects 0 = off, 1 = sounds that have a MIDI rule (snd_midimap.h) are played as MIDI events instead of being
 *   loaded and mixed (no WAV in memory, no mixing work), all other sounds stay WAV, 2 = like 1, and every other sound
 *   and all looping ambients are dropped (least CPU).
 * s_musicmode 0 = digital (CD), 1 = retro MIDI (music/bdgn_*.mid, played by the small sequencer below), 2 = off.
 * s_mididevice -1 = MIDI mapper (the device chosen in the Windows control panel), 0.. = a device from midi_devices.
 * s_midivolume 0..1 scales the velocity of effects and music.
 *
 * Channels (0 based): 0-8, 10, 11 music; 9 percussion (shared by effects and music); 12-15 melodic effects.
 */
#include <windows.h>
#include <mmsystem.h>
#include <process.h>
#include "../client/client.h"
#include "../client/snd_midi.h"
#include "snd_midimap.h"
#include "snd_wavmusic.h"

static cvar_t	*s_midieffects;
static cvar_t	*s_musicmode;
static cvar_t	*s_mididevice;
static cvar_t	*s_midivolume;
static cvar_t *s_midigmreset;
static cvar_t	*s_musicvolume;
static cvar_t	*s_nosound;

static HMIDIOUT	hmo;
static qboolean	midi_ok;
static qboolean	midi_tried;
static qboolean midi_registered;
static HANDLE midi_open_thread;
static HMIDIOUT midi_start_handle;
static volatile LONG midi_open_done, midi_open_cancel;
static MMRESULT midi_open_result;
static int midi_start_device, midi_used_device;
static int midi_start_gm;
static MMRESULT midi_start_sysex_error;
static int midi_pending_theme = -1;
static void Midi_PollOpen (void);
static void Midi_Close (void);
static int		efprog[4] = { -1, -1, -1, -1 };	/* program set on each effect channel, -1 = none (cleared at every open) */

/* ---------------------------------------------------------------- device */

static void Midi_Msg (int status, int d1, int d2)
{
	if (midi_ok)
		midiOutShortMsg (hmo, (DWORD)(status | (d1 << 8) | (d2 << 16)));
}

/* BDGN 3.50: a SysEx (the GM reset) is sent without waiting for the driver. The old code waited up to 100 x Sleep (2)
   for MHDR_DONE, which froze the game for a second or more on Win98 (coarse Sleep) with a slow driver, and then
   unprepared a header the driver could still own. The header and data are static so they stay valid until the driver is
   done; Midi_SysExRelease (every frame and at close) unprepares it then. Music waits for it (Midi_MusicUpdate). */
#define MIDI_RESET_WAIT		300			/* ms music waits at most for a GM reset the driver does not report done */

static MIDIHDR			sysex_hdr;
static unsigned char	sysex_buf[32];
static qboolean			sysex_busy;			/* the driver owns sysex_hdr */
static DWORD			sysex_sent;

static void Midi_SysExRelease (void)
{
	if (sysex_busy && (sysex_hdr.dwFlags & MHDR_DONE)
		&& midiOutUnprepareHeader (hmo, &sysex_hdr, sizeof(sysex_hdr)) == MMSYSERR_NOERROR)
		sysex_busy = false;		/* only now may sysex_hdr be used again */
}

static qboolean Midi_Wanted (void)
{
	return ( s_midieffects && s_midieffects->value ) || ( s_musicmode && (int)s_musicmode->value == 1 );
}

/* Some Win11 software synths block inside midiOutOpen/reset for seconds. Never run
   driver startup on the frame thread. Only this worker owns the unpublished handle. */
static unsigned __stdcall Midi_OpenThread (void *unused)
{
 MMRESULT result;
 MIDIOUTCAPS caps;
 int c;
 static unsigned char gm_on[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
 midi_used_device = midi_start_device;
 midi_start_sysex_error = MMSYSERR_NOERROR;
 midi_start_handle = NULL;
 result = midiOutOpen (&midi_start_handle, (UINT)midi_used_device, 0, 0, CALLBACK_NULL);
 if (result != MMSYSERR_NOERROR && midi_used_device != (int)MIDI_MAPPER)
 {
  midi_used_device = (int)MIDI_MAPPER;
  result = midiOutOpen (&midi_start_handle, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
 }
 midi_open_result = result;
 if (result == MMSYSERR_NOERROR && !midi_open_cancel)
 {
  for (c = 0; c < 16 && !midi_open_cancel; c++)
  {
   midiOutShortMsg (midi_start_handle, (DWORD)(0xB0 | c | (7 << 8) | (100 << 16)));
   midiOutShortMsg (midi_start_handle, (DWORD)(0xB0 | c | (64 << 8)));
  }
  /* Real hardware keeps its GM initialization. Avoid a needless GM SysEx reset on
     a software synth; some Win11 drivers stall on that reset. MIDI stays MIDI. */
  memset (&caps, 0, sizeof(caps));
  if (midi_start_gm && !midi_open_cancel && midiOutGetDevCaps ((UINT)midi_used_device, &caps, sizeof(caps)) == MMSYSERR_NOERROR
   && caps.wTechnology != MOD_SWSYNTH)
  {
   memset (&sysex_hdr, 0, sizeof(sysex_hdr));
   memcpy (sysex_buf, gm_on, sizeof(gm_on));
   sysex_hdr.lpData = (LPSTR)sysex_buf;
   sysex_hdr.dwBufferLength = sizeof(gm_on);
   sysex_hdr.dwBytesRecorded = sizeof(gm_on);
   if (midiOutPrepareHeader (midi_start_handle, &sysex_hdr, sizeof(sysex_hdr)) == MMSYSERR_NOERROR)
   {
    sysex_busy = true; /* also retained if the send fails */
    sysex_sent = timeGetTime ();
    midi_start_sysex_error = midiOutLongMsg (midi_start_handle, &sysex_hdr, sizeof(sysex_hdr));
   }
  }
 }
 InterlockedExchange (&midi_open_done, 1);
 return 0;
}

static void Midi_PollOpen (void)
{
 int c;
 /* BDGN 3.55x: a plain read of the volatile flag (aligned LONG, atomic on x86; the worker sets it with
    InterlockedExchange). InterlockedCompareExchange is not in Windows 95's KERNEL32 and stopped the game there. */
 if (!midi_open_thread || !midi_open_done) return;
 /* The worker may be between publishing and returning; poll instead of waiting. */
 if (WaitForSingleObject (midi_open_thread, 0) != WAIT_OBJECT_0) return;
 CloseHandle (midi_open_thread);
 midi_open_thread = NULL;
 if (midi_open_result != MMSYSERR_NOERROR)
 {
  midi_start_handle = NULL;
  midi_pending_theme = -1;
  Com_Printf ("MIDI: output open failed (%u), preferred device %i\n", (unsigned)midi_open_result, midi_start_device);
  return;
 }
 hmo = midi_start_handle;
 midi_start_handle = NULL;
 if (midi_open_cancel) { Midi_Close (); return; }
 midi_ok = true;
 for (c = 0; c < 4; c++) efprog[c] = -1;
 if (midi_start_sysex_error != MMSYSERR_NOERROR) Com_Printf ("MIDI: GM SysEx send failed (%u)\n", (unsigned)midi_start_sysex_error);
 Com_Printf ("MIDI: opened %s (%i)%s\n", midi_used_device == (int)MIDI_MAPPER ? "Windows MIDI Mapper" : "specific output",
  midi_used_device, midi_used_device != midi_start_device ? " (preferred device unavailable; session fallback)" : "");
}

static qboolean Midi_Open (void)
{
 unsigned id;
 Midi_PollOpen ();
 if (midi_ok) return true;
 if (midi_open_thread) return false;
 if (hmo) { Midi_Close (); if (hmo) return false; }
 if (midi_tried || !s_mididevice) return false;
 midi_tried = true;
 midi_start_device = (int)s_mididevice->value;
 midi_start_gm = s_midigmreset && s_midigmreset->value;
 s_mididevice->modified = false;
 InterlockedExchange (&midi_open_done, 0);
 InterlockedExchange (&midi_open_cancel, 0);
 midi_open_thread = (HANDLE)_beginthreadex (NULL, 0, Midi_OpenThread, NULL, 0, &id);
 if (!midi_open_thread) Com_Printf ("MIDI: could not start output initialization thread\n");
 return false;
}

static void Midi_Close (void)
{
 MMRESULT result;
 DWORD deadline;
 if (midi_open_thread) { InterlockedExchange (&midi_open_cancel, 1); return; }
 if (!hmo) return;
 midi_ok = false; /* retained handles are not usable for new events */
 result = midiOutReset (hmo);
 if (result != MMSYSERR_NOERROR) { Com_Printf ("MIDI: reset failed (%u); retaining device\n", (unsigned)result); return; }
 deadline = timeGetTime () + 500;
 if (sysex_busy)
 {
  do
  {
   result = midiOutUnprepareHeader (hmo, &sysex_hdr, sizeof(sysex_hdr));
   if (result != MIDIERR_STILLPLAYING) break;
   Sleep (1);
  } while ((LONG)(timeGetTime () - deadline) < 0);
  if (result != MMSYSERR_NOERROR) { Com_Printf ("MIDI: SysEx cleanup failed (%u); retaining device\n", (unsigned)result); return; }
  sysex_busy = false;
 }
 result = midiOutClose (hmo);
 if (result != MMSYSERR_NOERROR) { Com_Printf ("MIDI: close failed (%u); retaining device\n", (unsigned)result); return; }
 hmo = NULL;
}

qboolean MIDI_IsIdle (void)
{
 return hmo == NULL && !midi_open_thread;
}

int MIDI_DeviceCount (void)
{
 return (int)midiOutGetNumDevs ();
}

qboolean MIDI_DeviceName (int device, char *name, int size)
{
 MIDIOUTCAPS caps;
 if (!name || size < 1) return false;
 if (device == -1) { Com_sprintf (name, size, "Windows MIDI Mapper"); return true; }
 if (midiOutGetDevCaps ((UINT)device, &caps, sizeof(caps)) != MMSYSERR_NOERROR) return false;
 Com_sprintf (name, size, "%s", caps.szPname);
 return true;
}

/* BDGN 3.58d: the first MIDI output that the sound card plays itself: an FM, wavetable or square-wave synth or a
   MIDI port (MIDIOUTCAPS.wTechnology), not a software synth such as "Microsoft GS Wavetable SW Synth" (MOD_SWSYNTH)
   and not the mapper. -2 = none. For the Pa1983 benchmark (owner, 2026-11-20). */
int MIDI_HardwareDevice (void)
{
 MIDIOUTCAPS caps;
 UINT i, n = midiOutGetNumDevs ();
 for (i = 0 ; i < n ; i++)
 {
  if (midiOutGetDevCaps (i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
#ifdef MOD_SWSYNTH
  if (caps.wTechnology == MOD_SWSYNTH) continue;
#else
  if (caps.wTechnology == 7) continue;
#endif
  if (caps.wTechnology == MOD_MAPPER) continue;
  if (strstr (caps.szPname, "Microsoft") || strstr (caps.szPname, "SW Synth") || strstr (caps.szPname, "Software")) continue;
  return (int)i;
 }
 return -2;
}

/* ---------------------------------------------------------------- effects */

#define MAXACT		48
typedef struct { int chan, note, key; DWORD off; } midiact_t;
static midiact_t	act[MAXACT];
static int			nact;

static const int	efchan[4] = { 12, 13, 14, 15 };
static int			efnext;
static DWORD		lastfire[MIDI_NRULES + 1];

#define RCACHE		256
static struct { unsigned hash; short rule; } rcache[RCACHE];

static int Midi_Glob (const char *p, const char *s)
{
	while (*p)
	{
		if (*p == '*')
		{
			while (*p == '*')
				p++;
			if (!*p)
				return 1;
			for ( ; *s ; s++)
				if (Midi_Glob (p, s))
					return 1;
			return 0;
		}
		if (*p != *s)
			return 0;
		p++;
		s++;
	}
	return *s == 0;
}

/* -1 = no rule */
static int Midi_FindRule (const char *name)
{
	char		low[MAX_QPATH];
	unsigned	h = 5381;
	int			i;

	for (i = 0 ; name[i] && i < MAX_QPATH - 1 ; i++)
	{
		int c = (unsigned char)name[i];

		if (c >= 'A' && c <= 'Z')
			c += 32;
		if (c == '\\')
			c = '/';
		low[i] = (char)c;
		h = h * 33 + c;
	}
	low[i] = 0;
	if (rcache[h & (RCACHE - 1)].hash == h && rcache[h & (RCACHE - 1)].rule != 0)
		return rcache[h & (RCACHE - 1)].rule - 1 - 1;	/* stored as rule + 2, 1 = none */
	for (i = 0 ; i < (int)MIDI_NRULES ; i++)
		if (Midi_Glob (midi_rules[i].pat, low))
			break;
	if (i == (int)MIDI_NRULES)
		i = -1;
	rcache[h & (RCACHE - 1)].hash = h;
	rcache[h & (RCACHE - 1)].rule = (short)(i + 2);
	return i;
}

int MIDI_EffectsMode (void)
{
	if (!s_midieffects || !s_midieffects->value)
		return 0;
	if (!midi_ok && !Midi_Open ())
		return 0;
	return (int)s_midieffects->value;
}

int MIDI_MusicMode (void)
{
	return s_musicmode ? (int)s_musicmode->value : 0;
}

int MIDI_NoSound (void)
{
	return ( s_nosound && s_nosound->value ) ? 1 : 0;
}

qboolean MIDI_Maps (const char *name)
{
	if (!s_midieffects || !s_midieffects->value || name[0] == '*')
		return false;
	if (!Midi_Open ())
		return false;
	return Midi_FindRule (name) >= 0;
}

static void Midi_NoteOff (midiact_t *a)
{
	Midi_Msg (0x80 | a->chan, a->note, 0);
}

static void Midi_AddAct (int chan, int note, int key, DWORD off)
{
	int i;

	for (i = 0 ; i < nact ; i++)		/* same key and note: restart */
		if (act[i].chan == chan && act[i].note == note)
		{
			Midi_NoteOff (&act[i]);
			act[i].key = key;
			act[i].off = off;
			return;
		}
	if (nact == MAXACT)
	{
		Midi_NoteOff (&act[0]);
		memmove (&act[0], &act[1], sizeof(act[0]) * (MAXACT - 1));
		nact--;
	}
	act[nact].chan = chan;
	act[nact].note = note;
	act[nact].key = key;
	act[nact].off = off;
	nact++;
}

qboolean MIDI_PlayEffect (const char *name, int key, float volume)
{
	const midirule_t *r;
	int		ri, vel, i;
	DWORD	now;

	if (!s_midieffects || !s_midieffects->value || name[0] == '*')
		return false;
	if (!Midi_Open ())
		return false;
	ri = Midi_FindRule (name);
	if (ri < 0)
		return false;
	r = &midi_rules[ri];
	vel = (int)( r->vel * volume * s_midivolume->value * 1.25f );
	if (vel > 127)
		vel = 127;
	if (vel < 6)
		return true;					/* too quiet to matter: dropped, not mixed either */
	now = timeGetTime ();
	if (now - lastfire[ri] < 12)
		return true;					/* machine gun style floods */
	lastfire[ri] = now;

	if (r->kind == 'P')
	{
		Midi_Msg (0x99, r->note, vel);
		Midi_AddAct (9, r->note, key, now + 80);
		if (r->note2)
		{
			Midi_Msg (0x99, r->note2, vel * 9 / 10);
			Midi_AddAct (9, r->note2, key, now + 80);
		}
	}
	else
	{
		int c = efnext;

		efnext = (efnext + 1) & 3;
		if (efprog[c] != r->prog)
		{
			Midi_Msg (0xC0 | efchan[c], r->prog, 0);
			efprog[c] = r->prog;
		}
		for (i = 0 ; i < nact ; i++)		/* the same entity/channel replaces its own note */
			if (act[i].key == key && act[i].chan >= 12)
			{
				Midi_NoteOff (&act[i]);
				act[i].off = 0;
			}
		Midi_Msg (0x90 | efchan[c], r->note, vel);
		Midi_AddAct (efchan[c], r->note, key, now + r->dur);
	}
	return true;
}

void MIDI_StopEffect (int key)
{
	int i;

	for (i = 0 ; i < nact ; i++)
		if (act[i].key == key)
		{
			Midi_NoteOff (&act[i]);
			act[i].off = 0;
		}
}

void MIDI_StopEffects (void)
{
	int c;

	if (!midi_ok)
		return;
	for (c = 0 ; c < 4 ; c++)
		Midi_Msg (0xB0 | efchan[c], 123, 0);	/* all notes off */
	nact = 0;
}

/* ---------------------------------------------------------------- music: SMF sequencer */

typedef struct { int ms; unsigned char st, d1, d2; } mev_t;
typedef struct { unsigned long tick; unsigned long seq; unsigned char st, d1, d2; } tev_t;

static mev_t	*mev;
static int		nmev, mpos, mlen_ms;
static DWORD	mstart;
static qboolean	mplaying;

qboolean MIDI_IsPlaying (void) { return mplaying; }
qboolean MIDI_IsPlayingOn (int device) { return mplaying && midi_used_device == device; }
static int		mtheme = -1;
static qboolean	mclock;			/* BDGN 3.50: the music clock runs (set by the first Midi_MusicUpdate, not at load) */

#define MAXTEV		20000

static const char *theme_names[MIDI_NUMTHEMES] =
	{ "menu", "base", "industrial", "tech", "boss", "action", "victory" };

static unsigned long rd32 (const unsigned char *p) { return ((unsigned long)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static unsigned rd16 (const unsigned char *p) { return (p[0] << 8) | p[1]; }

static int tev_cmp (const void *a, const void *b)
{
	const tev_t *x = (const tev_t *)a, *y = (const tev_t *)b;

	if (x->tick != y->tick)
		return x->tick < y->tick ? -1 : 1;
	return x->seq < y->seq ? -1 : x->seq > y->seq;
}

static void Midi_FreeMusic (void)
{
	if (mev)
		Z_Free (mev);
	mev = NULL;
	nmev = 0;
	mplaying = false;
}

static qboolean Midi_ParseSMF (const unsigned char *d, int len)
{
	int		ntrk, div, t;
	unsigned fmt;
	const unsigned char *p, *end;
	tev_t	*tev;
	unsigned long *tempo_tick, *tempo_us;
	int		ntev = 0, ntempo = 0, i, ti;
	unsigned long seq = 0, endtick = 0;
	double	ms, ticklen;
	unsigned long lasttick;
	unsigned long usec;

	if (len < 14 || memcmp (d, "MThd", 4) || rd32 (d + 4) != 6)
		return false;
	fmt = rd16 (d + 8);
	ntrk = (int)rd16 (d + 10);
	div = (int)rd16 (d + 12);
	if (div & 0x8000 || !div || fmt > 1)
		return false;
	tev = (tev_t *)Z_Malloc (sizeof(tev_t) * MAXTEV);
	tempo_tick = (unsigned long *)Z_Malloc (sizeof(unsigned long) * 512 * 2);
	tempo_us = tempo_tick + 512;
	p = d + 14;
	for (t = 0 ; t < ntrk && p + 8 <= d + len ; t++)
	{
		unsigned long tick = 0, tlen = rd32 (p + 4);
		int running = 0;

		if (memcmp (p, "MTrk", 4))
			break;
		p += 8;
		end = p + tlen;
		if (end > d + len)
			end = d + len;
		while (p < end)
		{
			unsigned long delta = 0;
			int st;

			do { delta = (delta << 7) | (*p & 0x7f); } while ((*p++ & 0x80) && p < end);
			tick += delta;
			if (p >= end)
				break;
			st = *p;
			if (st == 0xFF)
			{
				int type = p[1];
				unsigned long ml = 0;

				p += 2;
				do { ml = (ml << 7) | (*p & 0x7f); } while ((*p++ & 0x80) && p < end);
				if (type == 0x51 && ml == 3 && ntempo < 512)
				{
					tempo_tick[ntempo] = tick;
					tempo_us[ntempo] = ((unsigned long)p[0] << 16) | (p[1] << 8) | p[2];
					ntempo++;
				}
				else if (type == 0x2F && tick > endtick)
					endtick = tick;
				p += ml;
				running = 0;
				continue;
			}
			if (st == 0xF0 || st == 0xF7)
			{
				unsigned long ml = 0;

				p++;
				do { ml = (ml << 7) | (*p & 0x7f); } while ((*p++ & 0x80) && p < end);
				p += ml;
				running = 0;
				continue;
			}
			if (st & 0x80)
			{
				running = st;
				p++;
			}
			else
				st = running;
			if (!st)
				break;
			if (ntev < MAXTEV)
			{
				tev[ntev].tick = tick;
				tev[ntev].seq = seq++;
				tev[ntev].st = (unsigned char)st;
				tev[ntev].d1 = p[0];
				tev[ntev].d2 = ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 0 : p[1];
				ntev++;
			}
			p += (((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 1 : 2);
		}
		p = end;
	}
	if (!ntev)
	{
		Z_Free (tev);
		Z_Free (tempo_tick);
		return false;
	}
	qsort (tev, ntev, sizeof(tev_t), tev_cmp);
	Midi_FreeMusic ();
	mev = (mev_t *)Z_Malloc (sizeof(mev_t) * ntev);
	/* ticks -> milliseconds through the tempo map (tempo events are in track order, sort them by tick: bubble, few) */
	for (i = 0 ; i < ntempo ; i++)
		for (ti = i + 1 ; ti < ntempo ; ti++)
			if (tempo_tick[ti] < tempo_tick[i])
			{
				unsigned long a = tempo_tick[i], b = tempo_us[i];

				tempo_tick[i] = tempo_tick[ti]; tempo_us[i] = tempo_us[ti];
				tempo_tick[ti] = a; tempo_us[ti] = b;
			}
	usec = 500000;
	ms = 0;
	lasttick = 0;
	ti = 0;
	for (i = 0 ; i < ntev ; i++)
	{
		while (ti < ntempo && tempo_tick[ti] <= tev[i].tick)
		{
			ms += (double)(tempo_tick[ti] - lasttick) * usec / 1000.0 / div;
			lasttick = tempo_tick[ti];
			usec = tempo_us[ti];
			ti++;
		}
		ticklen = (double)(tev[i].tick - lasttick) * usec / 1000.0 / div;
		mev[i].ms = (int)(ms + ticklen);
		mev[i].st = tev[i].st;
		mev[i].d1 = tev[i].d1;
		mev[i].d2 = tev[i].d2;
	}
	nmev = ntev;
	/* loop length: the end of track marker */
	{
		double e = 0;

		lasttick = 0; usec = 500000; ti = 0; ms = 0;
		while (ti < ntempo && tempo_tick[ti] <= endtick)
		{
			ms += (double)(tempo_tick[ti] - lasttick) * usec / 1000.0 / div;
			lasttick = tempo_tick[ti];
			usec = tempo_us[ti];
			ti++;
		}
		e = ms + (double)(endtick - lasttick) * usec / 1000.0 / div;
		mlen_ms = (int)e;
		if (mlen_ms < mev[nmev - 1].ms + 1)
			mlen_ms = mev[nmev - 1].ms + 500;
	}
	Z_Free (tev);
	Z_Free (tempo_tick);
	return true;
}

static void Midi_MusicSilence (void)
{
	int c;

	if (!midi_ok)
		return;
	for (c = 0 ; c < 12 ; c++)
	{
		if (c == 9)
			continue;
		Midi_Msg (0xB0 | c, 123, 0);		/* all notes off */
		Midi_Msg (0xB0 | c, 121, 0);		/* reset controllers */
	}
	for (c = 35 ; c < 82 ; c++)
		Midi_Msg (0x89, c, 0);
}

void MIDI_StopMusic (void)
{
	if (mplaying)
		Midi_MusicSilence ();
	Midi_FreeMusic ();
	mtheme = -1;
}

void MIDI_SetMusicVolume (float volume)
{
	int c, value;
	if (volume < 0) volume = 0;
	if (volume > 1) volume = 1;
	if (s_musicvolume) Cvar_SetValue ("s_musicvolume", volume);
	value = (int)(volume * 127.0f);
	for (c = 0; c < 12; c++)
		if (c != 9) Midi_Msg (0xB0 | c, 7, value);
}

void MIDI_PlayTheme (int theme)
{
	char	path[MAX_QPATH];
	byte	*buf;
	int		len;

	if (theme < 0 || theme >= MIDI_NUMTHEMES)
		return;
	if (!s_musicmode || (int)s_musicmode->value != 1)
		return;
	if (mplaying && mtheme == theme)
		return;
	WavMusic_Stop ();
	if (!WavMusic_IsIdle () || !CDAudio_ReleaseDevice ()) { midi_pending_theme = -1; return; }
	if (!midi_ok)
	{
		if (!midi_open_thread)
		{
			CDAudio_Stop ();
			if (!CDAudio_ReleaseDevice () || !WavMusic_IsIdle () || !MIDI_IsIdle ()) { midi_pending_theme = -1; return; }
		}
		midi_pending_theme = theme;
		if (!Midi_Open ()) return;
	}
	midi_pending_theme = -1;
	MIDI_StopMusic ();
	Com_sprintf (path, sizeof(path), "music/bdgn_%s.mid", theme_names[theme]);
	len = FS_LoadFile (path, (void **)&buf);
	if (len <= 0 || !buf)
	{
		Com_Printf ("MIDI: %s not found (put the music folder in baseq2)\n", path);
		return;
	}
	if (!Midi_ParseSMF (buf, len))
		Com_Printf ("MIDI: cannot read %s\n", path);
	else
	{
		mtheme = theme;
		mpos = 0;
		mclock = false;		/* the clock starts in Midi_MusicUpdate, so a slow frame after loading drops no notes */
		mplaying = true;
	}
	FS_FreeFile (buf);
}

typedef struct { const char *prefix; int theme; } maptheme_t;
static const maptheme_t map_themes[] =
{
	{ "boss", MIDI_THEME_BOSS },
	{ "base", MIDI_THEME_BASE }, { "train", MIDI_THEME_BASE }, { "bunk", MIDI_THEME_BASE },
	{ "jail", MIDI_THEME_BASE }, { "security", MIDI_THEME_BASE },
	{ "mine", MIDI_THEME_INDUSTRIAL }, { "ware", MIDI_THEME_INDUSTRIAL }, { "fact", MIDI_THEME_INDUSTRIAL },
	{ "power", MIDI_THEME_INDUSTRIAL }, { "cool", MIDI_THEME_INDUSTRIAL }, { "waste", MIDI_THEME_INDUSTRIAL },
	{ "mintro", MIDI_THEME_INDUSTRIAL },
	{ "lab", MIDI_THEME_TECH }, { "command", MIDI_THEME_TECH }, { "hangar", MIDI_THEME_TECH },
	{ "strike", MIDI_THEME_TECH }, { "space", MIDI_THEME_TECH }, { "city", MIDI_THEME_TECH },
	{ "biggun", MIDI_THEME_TECH },
	{ "q2dm", MIDI_THEME_ACTION },
	{ NULL, 0 }
};

void MIDI_PlayMapMusic (int cdtrack)
{
	const char *m = cl.configstrings[CS_MODELS + 1];
	int		i, theme = MIDI_THEME_ACTION;

	if (!strncmp (m, "maps/", 5))
		m += 5;
	for (i = 0 ; map_themes[i].prefix ; i++)
		if (!Q_strncasecmp ((char *)m, (char *)map_themes[i].prefix, (int)strlen (map_themes[i].prefix)))
		{
			theme = map_themes[i].theme;
			break;
		}
	if (!map_themes[i].prefix && cdtrack >= 2 && cdtrack <= 11)	/* unknown (mod) map: by the CD track of the map */
		theme = (cdtrack <= 4) ? MIDI_THEME_BASE : (cdtrack <= 7) ? MIDI_THEME_INDUSTRIAL : (cdtrack <= 10) ? MIDI_THEME_TECH : MIDI_THEME_BOSS;
	MIDI_PlayTheme (theme);
}

void MIDI_MenuMusic (void)
{
	if (MIDI_MusicMode () == 1)
		MIDI_PlayTheme (MIDI_THEME_MENU);
}

static void Midi_MusicUpdate (DWORD now)
{
	int el;

	if (!mplaying || !midi_ok)
		return;
	if (!mclock)
	{	/* start when the GM reset is done (notes sent before it could be cut), or after MIDI_RESET_WAIT at most */
		if (sysex_busy && (int)(now - sysex_sent) < MIDI_RESET_WAIT)
			return;
		mstart = now;
		mpos = 0;
		mclock = true;
	}
	el = (int)(now - mstart);
	if (el >= mlen_ms)
	{
		Midi_MusicSilence ();
		mstart += mlen_ms;
		el = (int)(now - mstart);
		if (el >= mlen_ms)
		{
			mstart = now;
			el = 0;
		}
		mpos = 0;
	}
	while (mpos < nmev && mev[mpos].ms <= el)
	{
		const mev_t *e = &mev[mpos++];
		int kind = e->st & 0xF0;

		if (kind == 0x90 && e->d2)
		{
			int v;

			if (el - e->ms > 250)
				continue;			/* far behind (loading, minimized): skip the late note */
			if (s_musicvolume->value <= 0)
				continue;
			v = (int)(e->d2 * s_midivolume->value * s_musicvolume->value * 1.25f);
			if (v > 127) v = 127;
			if (v < 1) v = 1;
			Midi_Msg (e->st, e->d1, v);
		}
		else if (kind == 0xC0 || kind == 0xD0)
			Midi_Msg (e->st, e->d1, 0);
		else
			Midi_Msg (e->st, e->d1, e->d2);
	}
}

/* ---------------------------------------------------------------- frame, init, console */

void MIDI_Update (void)
{
	DWORD	now;
	int		i;

	Midi_PollOpen ();
	if (s_mididevice && s_mididevice->modified)
	{
		int theme = mtheme >= 0 ? mtheme : midi_pending_theme;
		MIDI_Shutdown ();
		s_mididevice->modified = false;
		if (theme >= 0) MIDI_PlayTheme (theme);
	}
	if (midi_pending_theme >= 0 && MIDI_MusicMode () == 1) MIDI_PlayTheme (midi_pending_theme);
	if (!midi_ok)
		return;
	Midi_SysExRelease ();
	now = timeGetTime ();
	for (i = 0 ; i < nact ; )
	{
		if (!act[i].off || (long)(now - act[i].off) >= 0)
		{
			Midi_NoteOff (&act[i]);
			act[i] = act[--nact];
		}
		else
			i++;
	}
	Midi_MusicUpdate (now);
	if (!Midi_Wanted () && !mplaying)
	{	/* nothing uses MIDI any more: give the device back */
		MIDI_StopEffects ();
		Midi_Close ();
		midi_tried = false;
	}
}

static void Midi_Devices_f (void)
{
	UINT n = midiOutGetNumDevs (), i;
	MIDIOUTCAPS caps;

	Com_Printf ("MIDI output devices (s_mididevice, -1 = MIDI mapper):\n");
	for (i = 0 ; i < n ; i++)
		if (midiOutGetDevCaps (i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
			Com_Printf ("  %u: %s%s\n", i, caps.szPname,
			            caps.wTechnology == MOD_MIDIPORT ? " (external MIDI port)" : "");
	if (!n)
		Com_Printf ("  none\n");
}

static void Midi_Reset_f (void)
{
	MIDI_StopMusic ();
	MIDI_StopEffects ();
	Midi_Close ();
	midi_tried = false;		/* Midi_Open clears the effect programs */
}

static void Midi_Play_f (void)
{
	int i;

	if (Cmd_Argc () < 2)
	{
		Com_Printf ("usage: midi_play menu|base|industrial|tech|boss|action|victory (needs s_musicmode 1)\n");
		return;
	}
	for (i = 0 ; i < MIDI_NUMTHEMES ; i++)
		if (!Q_stricmp (Cmd_Argv (1), (char *)theme_names[i]))
		{
			MIDI_PlayTheme (i);
			return;
		}
	Com_Printf ("unknown theme\n");
}

static void Midi_Stop_f (void)
{
	MIDI_StopMusic ();
}

/* midi_effect weapons/rocklf1a.wav : plays one sound through the rules (needs s_midieffects 1) */
static void Midi_Effect_f (void)
{
	if (Cmd_Argc () < 2)
	{
		Com_Printf ("usage: midi_effect <sound name, e.g. weapons/shotgf1b.wav>\n");
		return;
	}
	if (!MIDI_PlayEffect (Cmd_Argv (1), 0, 1.0f))
		Com_Printf ("not played (s_midieffects is 0, the device is not open, or no rule for that name)\n");
}

void MIDI_Init (void)
{
	s_midieffects = Cvar_Get ("s_midieffects", "0", CVAR_ARCHIVE);
	s_musicmode = Cvar_Get ("s_musicmode", "0", CVAR_ARCHIVE);
	s_mididevice = Cvar_Get ("s_mididevice", "-1", CVAR_ARCHIVE);
	s_midigmreset = Cvar_Get ("s_midigmreset", "1", CVAR_ARCHIVE);
	s_midivolume = Cvar_Get ("s_midivolume", "0.8", CVAR_ARCHIVE);
	s_musicvolume = Cvar_Get ("s_musicvolume", "1.0", CVAR_ARCHIVE);
	s_nosound = Cvar_Get ("s_nosound", "0", CVAR_ARCHIVE);
	if (midi_registered) return;
	midi_registered = true;
	s_mididevice->modified = false;
	Cmd_AddCommand ("midi_devices", Midi_Devices_f);
	Cmd_AddCommand ("midi_reset", Midi_Reset_f);
	Cmd_AddCommand ("midi_play", Midi_Play_f);
	Cmd_AddCommand ("midi_stop", Midi_Stop_f);
	Cmd_AddCommand ("midi_effect", Midi_Effect_f);
}

void MIDI_Shutdown (void)
{
	midi_pending_theme = -1;
	MIDI_StopMusic ();
	MIDI_StopEffects ();
	Midi_Close ();
	midi_tried = false;		/* back to the state after MIDI_Init */
}

/* Only final process shutdown may wait briefly; level changes never join this worker. */
void MIDI_FinalShutdown (void)
{
 MIDI_Shutdown ();
 if (midi_open_thread)
 {
  if (WaitForSingleObject (midi_open_thread, 1000) == WAIT_OBJECT_0) Midi_PollOpen ();
  else Com_Printf ("MIDI: driver startup still pending at exit; process cleanup will release it\n");
 }
 Midi_Close ();
}
