/*
snd_pcspk.c -- BDGN 3.58g: "PC speaker" sound mode (owner, 2026-11-20: "klassiskt Wolf3D-liknande ljud ur PC-speakern").

Sound mode "PC speaker" (s_pcspeaker 1, Options -> sound mode): the game's sound effects are not mixed; the sounds
that matter are played as short square-wave effects on the PC speaker, in the manner of Wolfenstein 3D: one effect at a
time, a new one only replaces the playing one when it is at least as important, frequencies change 140 times a second.
Music is not touched. Online: only what this computer plays changes.

The effects are BDGN's own, made here from frequency sweeps and noise (no sounds from other games).

The speaker (s_pcspeaker 1, "PC speaker", automatic):
- Windows 95 / 98 / ME: programmed directly (timer channel 2 on ports 0x42/0x43, speaker gate on port 0x61); these
  systems allow port access from a program.
- Windows NT 4 / 2000 / XP: Beep(frequency, ms), which drives the real speaker there.
- Windows Vista and later: through the sound card (below). Beep() there is an emulation Windows plays through the
  sound card anyway, and on many computers it is silent; modern computers seldom have a speaker at all.
A worker thread plays the effects on the speaker so the game never waits for them. VS2005 C, no CRT calls in it.

BDGN 3.58l (owner, 2026-11-20: "fallback ... ur mina hörlurar om min dator saknar PC speaker"): the sound card output.
"PC speaker (sound card)" (s_pcspeaker 2), and the automatic mode on Vista and later: the same effects as a square
wave mixed into the game's own sound output (PCSPK_Paint, called by the mixer in client/snd_mix.c), so they come out
of the speakers or headphones like every other sound, at the sound volume. 140 steps a second counted in samples, so
the timing is exact; the delay is the mixer's own (the same as every sound). Whether a speaker is fitted cannot be
detected (the timer chip is always there, the little loudspeaker is not), so on Windows 9x / 2000 / XP the menu choice
decides; without a running sound card the automatic mode uses the speaker.
*/
#include <windows.h>
#include <conio.h>
#include "../client/client.h"
#include "../client/snd_loc.h"
#include "../client/snd_pcspk.h"

extern int sound_started;	/* client/snd_dma.c */

#define PCSPK_HZ		140		/* Wolfenstein 3D's PC speaker rate */
#define PCSPK_MAXTICKS	64
#define PCSPK_EFFECTS	24

enum { E_NONE, E_SHOTGUN, E_SSHOTGUN, E_MACHGUN, E_CHAINGUN, E_BLASTER, E_HYPER, E_RAIL, E_ROCKET, E_GRENADE, E_EXPLODE,
	E_BFG, E_PAIN, E_DEATH, E_PICKUP, E_HEALTH, E_WEAPONUP, E_DOOR, E_SIGHT, E_NOAMMO, E_MENU, E_THUD, E_JUMP, E_SECRET,
	E_TELEPORT, E_COUNT };

typedef struct { const char *pat; unsigned char effect, prio, own_only; } pcrule_t;
/* first match wins; a pattern matches when it occurs in the sound's name */
static const pcrule_t pc_rules[] = {
	{ "death", E_DEATH, 9, 0 }, { "die", E_DEATH, 9, 0 },
	{ "weapons/bfg__f", E_BFG, 8, 0 }, { "weapons/bfg__x", E_EXPLODE, 8, 0 },
	{ "weapons/rocklx", E_EXPLODE, 8, 0 }, { "weapons/grenlx", E_EXPLODE, 8, 0 }, { "weapons/xpld", E_EXPLODE, 8, 0 },
	{ "explod", E_EXPLODE, 8, 0 },
	{ "weapons/railgf", E_RAIL, 7, 0 }, { "weapons/rocklf", E_ROCKET, 7, 0 }, { "weapons/sshotf", E_SSHOTGUN, 7, 0 },
	{ "weapons/shotgf", E_SHOTGUN, 7, 0 }, { "misc/secret", E_SECRET, 7, 0 },
	{ "weapons/machgf", E_MACHGUN, 6, 0 }, { "weapons/chngnl", E_CHAINGUN, 6, 0 }, { "weapons/blastf", E_BLASTER, 6, 0 },
	{ "weapons/hyprbf", E_HYPER, 6, 0 }, { "weapons/grenlf", E_GRENADE, 6, 0 }, { "misc/tele", E_TELEPORT, 6, 0 },
	{ "pain", E_PAIN, 6, 0 },
	{ "misc/w_pkup", E_WEAPONUP, 5, 1 }, { "items/s_health", E_HEALTH, 5, 1 }, { "items/n_health", E_HEALTH, 5, 1 },
	{ "items/m_health", E_HEALTH, 5, 1 }, { "items/l_health", E_HEALTH, 5, 1 }, { "items/pkup", E_PICKUP, 5, 1 },
	{ "misc/am_pkup", E_PICKUP, 5, 1 }, { "misc/ar1_pkup", E_PICKUP, 5, 1 }, { "misc/ar2_pkup", E_PICKUP, 5, 1 },
	{ "misc/ar3_pkup", E_PICKUP, 5, 1 }, { "weapons/hgrena", E_GRENADE, 5, 1 },
	{ "sight", E_SIGHT, 4, 0 }, { "fall", E_THUD, 4, 1 },
	{ "doors/", E_DOOR, 3, 0 }, { "plats/", E_DOOR, 2, 0 }, { "weapons/noammo", E_NOAMMO, 3, 1 },
	{ "misc/menu", E_MENU, 2, 1 }, { "misc/talk", E_MENU, 3, 1 }, { "jump", E_JUMP, 2, 1 },
	{ NULL, 0, 0, 0 }
};

static unsigned short pc_fx[E_COUNT][PCSPK_MAXTICKS];
static int pc_fxlen[E_COUNT];

static cvar_t *s_pcspeaker;
static HANDLE pc_thread;
static volatile LONG pc_run;
static CRITICAL_SECTION pc_cs;
static int pc_cs_ok, pc_win9x;
/* the playing effect (under pc_cs) */
static int pc_effect, pc_pos, pc_prio;
/* BDGN 3.58l: where the effects go: 0 nowhere, 1 the speaker (worker thread), 2 the sound card (PCSPK_Paint) */
enum { PCOUT_NONE, PCOUT_SPEAKER, PCOUT_SOUNDCARD };
static int pc_out;
static int pc_want_seen = -1, pc_snd_seen = -1;
/* the sound card output (mixer side, under pc_cs) */
static int pc_soft_hz, pc_soft_left;
static unsigned pc_soft_phase;

static unsigned pc_seed = 0x1983;
static int Pc_Rand (int lo, int hi)
{
	pc_seed = pc_seed * 1103515245u + 12345u;
	return lo + (int)((pc_seed >> 16) % (unsigned)(hi - lo + 1));
}
static void Pc_Sweep (int e, int ticks, int f0, int f1, int jitter)
{
	int i, n = pc_fxlen[e];
	for (i = 0 ; i < ticks && n < PCSPK_MAXTICKS ; i++)
	{
		int f = f0 + (f1 - f0) * i / (ticks > 1 ? ticks - 1 : 1);
		if (jitter) f += Pc_Rand (-jitter, jitter);
		pc_fx[e][n++] = (unsigned short)(f < 40 ? 40 : f);
	}
	pc_fxlen[e] = n;
}
static void Pc_Noise (int e, int ticks, int lo, int hi, int fall)
{
	int i, n = pc_fxlen[e];
	for (i = 0 ; i < ticks && n < PCSPK_MAXTICKS ; i++)
	{
		int h = hi - (hi - lo) * i * fall / (ticks * 100);
		pc_fx[e][n++] = (unsigned short)Pc_Rand (lo, h > lo ? h : lo + 1);
	}
	pc_fxlen[e] = n;
}
static void Pc_Notes (int e, const int *f, int count, int each)
{
	int i, k;
	for (i = 0 ; i < count ; i++)
		for (k = 0 ; k < each && pc_fxlen[e] < PCSPK_MAXTICKS ; k++)
			pc_fx[e][pc_fxlen[e]++] = (unsigned short)f[i];
}
static void Pc_Silence (int e, int ticks)
{
	while (ticks-- > 0 && pc_fxlen[e] < PCSPK_MAXTICKS)
		pc_fx[e][pc_fxlen[e]++] = 0;
}
static void Pc_BuildEffects (void)
{
	static const int pickup[] = { 600, 800, 1000, 1300 }, health[] = { 500, 700, 900 };
	static const int weaponup[] = { 400, 500, 600, 800, 1000, 1200 }, sight[] = { 400, 600, 400, 600 };
	static const int secret[] = { 800, 1000, 1200, 1600 };
	int i;
	memset (pc_fxlen, 0, sizeof(pc_fxlen));
	Pc_Noise (E_SHOTGUN, 6, 150, 420, 0); Pc_Sweep (E_SHOTGUN, 4, 300, 120, 0);
	Pc_Noise (E_SSHOTGUN, 10, 100, 380, 50); Pc_Sweep (E_SSHOTGUN, 4, 200, 80, 0);
	Pc_Sweep (E_MACHGUN, 3, 700, 300, 40);
	Pc_Sweep (E_CHAINGUN, 2, 600, 350, 30);
	Pc_Sweep (E_BLASTER, 8, 1800, 900, 0);
	Pc_Sweep (E_HYPER, 4, 1600, 1100, 0);
	Pc_Sweep (E_RAIL, 6, 2400, 1800, 0); Pc_Sweep (E_RAIL, 14, 1800, 250, 120);
	Pc_Sweep (E_ROCKET, 12, 200, 650, 60);
	Pc_Sweep (E_GRENADE, 6, 420, 240, 0);
	Pc_Noise (E_EXPLODE, 28, 60, 420, 80);
	Pc_Sweep (E_BFG, 20, 100, 1500, 0); Pc_Noise (E_BFG, 16, 80, 600, 60);
	Pc_Sweep (E_PAIN, 8, 900, 300, 30);
	for (i = 0 ; i < 6 ; i++) Pc_Sweep (E_DEATH, 5, 800 - i * 110, 600 - i * 90, 20);
	Pc_Notes (E_PICKUP, pickup, 4, 2);
	Pc_Notes (E_HEALTH, health, 3, 2);
	Pc_Notes (E_WEAPONUP, weaponup, 6, 2);
	for (i = 0 ; i < 9 ; i++) Pc_Sweep (E_DOOR, 2, i & 1 ? 120 : 140, i & 1 ? 120 : 140, 0);
	Pc_Notes (E_SIGHT, sight, 4, 3);
	Pc_Sweep (E_NOAMMO, 2, 150, 150, 0);
	Pc_Sweep (E_MENU, 2, 1000, 1000, 0);
	Pc_Sweep (E_THUD, 4, 100, 60, 0);
	Pc_Sweep (E_JUMP, 4, 300, 600, 0);
	Pc_Notes (E_SECRET, secret, 4, 3);
	Pc_Sweep (E_TELEPORT, 15, 200, 2000, 0); Pc_Sweep (E_TELEPORT, 15, 2000, 200, 0);
	for (i = 0 ; i < E_COUNT ; i++)
		Pc_Silence (i, 1);	/* the speaker goes quiet at the end of every effect */
}

/* ------------------------------------------------------------------ speaker */

static void Pc_Tone (int hz)
{
	if (pc_win9x)
	{	/* Windows 9x: the timer chip's channel 2 drives the speaker */
		if (hz > 0)
		{
			unsigned div = 1193180u / (unsigned)hz;
			_outp (0x43, 0xB6);
			_outp (0x42, div & 0xFF);
			_outp (0x42, (div >> 8) & 0xFF);
			_outp (0x61, _inp (0x61) | 3);
		}
		else
			_outp (0x61, _inp (0x61) & 0xFC);
	}
}

static DWORD WINAPI Pc_Thread (LPVOID unused)
{
	const DWORD tick = 1000 / PCSPK_HZ;	/* 7 ms */
	int last = -1;
	(void)unused;
	while (pc_run)
	{
		int hz = 0, run = 1;
		EnterCriticalSection (&pc_cs);
		if (pc_effect && pc_pos < pc_fxlen[pc_effect])
		{
			hz = pc_fx[pc_effect][pc_pos++];
			if (!pc_win9x)	/* Beep: one call for a run of the same tone */
				while (pc_pos < pc_fxlen[pc_effect] && pc_fx[pc_effect][pc_pos] == hz && run < 20)
				{
					pc_pos++;
					run++;
				}
		}
		else
		{
			pc_effect = 0;
			pc_prio = 0;
		}
		LeaveCriticalSection (&pc_cs);
		if (pc_win9x)
		{
			if (hz != last)
				Pc_Tone (hz);
			last = hz;
			Sleep (tick);
		}
		else if (hz >= 37)
			Beep ((DWORD)hz, tick * run);	/* waits for the tone */
		else
			Sleep (tick * run);
	}
	if (pc_win9x)
		Pc_Tone (0);
	return 0;
}

/* BDGN 3.58l: Windows Vista or later (NT 6+) */
static qboolean Pc_VistaOrLater (void)
{
	OSVERSIONINFO v;
	memset (&v, 0, sizeof(v));
	v.dwOSVersionInfoSize = sizeof(v);
	if (!GetVersionEx (&v))
		return false;
	return v.dwPlatformId == VER_PLATFORM_WIN32_NT && v.dwMajorVersion >= 6;
}

/* BDGN 3.58w (owner 2026-11-21: the headphones when the computer has no PC speaker): Windows lists the speaker as the
   Plug and Play device PNP0800 ("System speaker") when the BIOS reports one. A program cannot hear whether a speaker
   is wired, so this is the best sign there is (VERIFIERAD ANALYS of the registry layout, not tried on every machine):
   - Windows 95 / 98 / ME: HKLM\Enum\BIOS\*PNP0800 (or Enum\Root\*PNP0800);
   - Windows NT 4 / 2000 / XP: HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\PNP0800, Enum\Root\*PNP0800 or the Beep
     driver (Enum\Root\LEGACY_BEEP).
   The speaker counts as missing only when Windows has the device list (Enum\BIOS or Enum\ACPI) and the speaker is
   not in it; when there is no list (old machines without a Plug and Play BIOS) it counts as there. */
static qboolean Pc_KeyExists (const char *path)
{
	HKEY k;
	if (RegOpenKeyEx (HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k) != ERROR_SUCCESS)
		return false;
	RegCloseKey (k);
	return true;
}
static qboolean Pc_SpeakerMissing (void)
{
	static int missing = -1;
	if (missing < 0)
	{
		if (GetVersion () & 0x80000000)
			missing = Pc_KeyExists ("Enum\\BIOS") && !Pc_KeyExists ("Enum\\BIOS\\*PNP0800") &&
				!Pc_KeyExists ("Enum\\Root\\*PNP0800");
		else
			missing = Pc_KeyExists ("SYSTEM\\CurrentControlSet\\Enum\\ACPI") &&
				!Pc_KeyExists ("SYSTEM\\CurrentControlSet\\Enum\\ACPI\\PNP0800") &&
				!Pc_KeyExists ("SYSTEM\\CurrentControlSet\\Enum\\Root\\*PNP0800") &&
				!Pc_KeyExists ("SYSTEM\\CurrentControlSet\\Enum\\Root\\LEGACY_BEEP");
		if (missing)
			Com_Printf ("PC speaker: none listed by Windows, using the sound card\n");
	}
	return missing != 0;
}

static void Pc_Prepare (void)
{
	pc_win9x = (GetVersion () & 0x80000000) != 0;
	if (!pc_cs_ok)
	{
		InitializeCriticalSection (&pc_cs);
		pc_cs_ok = 1;
	}
	Pc_BuildEffects ();
	pc_effect = pc_pos = pc_prio = 0;
	pc_soft_hz = pc_soft_left = 0;
	pc_soft_phase = 0;
}

static qboolean Pc_Start (void)
{
	DWORD id;
	if (pc_thread)
		return true;
	Pc_Prepare ();
	pc_run = 1;
	pc_thread = CreateThread (NULL, 0, Pc_Thread, NULL, 0, &id);
	if (!pc_thread)
	{
		pc_run = 0;
		return false;
	}
	SetThreadPriority (pc_thread, THREAD_PRIORITY_ABOVE_NORMAL);
	Com_Printf ("PC speaker: %s\n", pc_win9x ? "direct (Windows 9x)" : "Beep (Windows NT)");
	return true;
}

/* ------------------------------------------------------------------ public */

int PCSPK_Mode (void)
{
	int want, out;
	if (!s_pcspeaker)
		s_pcspeaker = Cvar_Get ("s_pcspeaker", "0", CVAR_ARCHIVE);
	want = (int)s_pcspeaker->value;
	if (want <= 0)
	{
		if (pc_out)
			PCSPK_Shutdown ();
		return 0;
	}
	if (want > 2)
		want = 1;
	if (pc_out && want == pc_want_seen && sound_started == pc_snd_seen)
		return 1;	/* the usual case: decided already */
	/* BDGN 3.58l: choose the output */
	if (want == 2)
		out = sound_started ? PCOUT_SOUNDCARD : PCOUT_SPEAKER;
	else
		out = (sound_started && (Pc_VistaOrLater () || Pc_SpeakerMissing ())) ? PCOUT_SOUNDCARD : PCOUT_SPEAKER;
	if (out != pc_out)
	{
		PCSPK_Shutdown ();
		if (out == PCOUT_SPEAKER)
		{
			if (!Pc_Start ())
				return 0;
		}
		else
		{
			Pc_Prepare ();
			Com_Printf ("PC speaker: through the sound card%s\n", want == 1 ? " (Windows Vista or later)" : "");
		}
		pc_out = out;
	}
	pc_want_seen = want;
	pc_snd_seen = sound_started;
	return 1;
}

/* BDGN 3.58l: the sound card output. Adds the playing effect as a square wave to the mixer's paint buffer (count
   stereo samples at rate Hz, values in the mixer's scale: 16-bit sample << 8). Called by S_PaintChannels for every
   block it paints; does nothing unless the effects go to the sound card. */
void PCSPK_Paint (void *buffer, int count, int rate)
{
	portable_samplepair_t	*pb = (portable_samplepair_t *)buffer;
	int		i, amp, step, tick;
	float	vol;
	if (pc_out != PCOUT_SOUNDCARD || !pc_cs_ok || count <= 0 || rate <= 0)
		return;
	vol = Cvar_VariableValue ("s_volume");
	if (vol < 0) vol = 0;
	if (vol > 1) vol = 1;
	amp = (int)(7000.0f * 256.0f * vol);	/* about a fifth of full scale at volume 1: a square wave is loud */
	tick = rate / PCSPK_HZ;
	if (tick < 1) tick = 1;
	EnterCriticalSection (&pc_cs);
	for (i = 0 ; i < count ; i++)
	{
		if (pc_soft_left <= 0)
		{	/* the next 1/140 s step of the effect */
			if (pc_effect && pc_pos < pc_fxlen[pc_effect])
				pc_soft_hz = pc_fx[pc_effect][pc_pos++];
			else
			{
				pc_effect = pc_prio = 0;
				pc_soft_hz = 0;
			}
			pc_soft_left = tick;
		}
		pc_soft_left--;
		if (pc_soft_hz >= 37 && amp)
		{
			step = (int)(((unsigned)pc_soft_hz << 16) / (unsigned)rate);	/* phase step, 16.16 */
			pc_soft_phase += (unsigned)step;
			if (pc_soft_phase & 0x8000)
			{
				pb[i].left += amp;
				pb[i].right += amp;
			}
			else
			{
				pb[i].left -= amp;
				pb[i].right -= amp;
			}
		}
	}
	LeaveCriticalSection (&pc_cs);
}

void PCSPK_Sound (const char *name, qboolean own, float distance)
{
	const pcrule_t *r;
	if (!name || !pc_out)
		return;
	for (r = pc_rules ; r->pat ; r++)
		if (strstr (name, r->pat))
			break;
	if (!r->pat)
		return;
	if (r->own_only && !own)
		return;
	if (!own && distance > 900)
		return;	/* too far away to hear */
	{
		int prio = r->prio - (own ? 0 : 1);	/* the own sounds win over the same kind from others */
		EnterCriticalSection (&pc_cs);
		if (!pc_effect || prio >= pc_prio)
		{
			pc_effect = r->effect;
			pc_pos = 0;
			pc_prio = prio;
		}
		LeaveCriticalSection (&pc_cs);
	}
}

void PCSPK_Stop (void)
{
	if (!pc_out || !pc_cs_ok)
		return;
	EnterCriticalSection (&pc_cs);
	pc_effect = pc_pos = pc_prio = 0;
	LeaveCriticalSection (&pc_cs);
}

void PCSPK_Shutdown (void)
{
	pc_want_seen = pc_snd_seen = -1;
	if (pc_out == PCOUT_SOUNDCARD && pc_cs_ok)
	{	/* 3.58l: nothing to stop but the effect */
		EnterCriticalSection (&pc_cs);
		pc_effect = pc_pos = pc_prio = 0;
		pc_soft_hz = pc_soft_left = 0;
		LeaveCriticalSection (&pc_cs);
	}
	pc_out = PCOUT_NONE;
	if (!pc_thread)
		return;
	pc_run = 0;
	WaitForSingleObject (pc_thread, 2000);
	CloseHandle (pc_thread);
	pc_thread = NULL;
	if (pc_win9x)
		Pc_Tone (0);
}
