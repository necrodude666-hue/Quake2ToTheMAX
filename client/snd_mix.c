/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// snd_mix.c -- portable code to mix sounds for snd_dma.c

#include "client.h"
#include "snd_loc.h"
#include "snd_pcspk.h"	/* BDGN 3.58l */
#include "../qcommon/q3e_3dnow.h"	// BDGN 3.50: Q3E_PaintFrom8 / Q3E_PaintFrom16, Q3E_CPUHas3DNow
#include <mmintrin.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#include "../qcommon/software_smp.h"
#endif

#define	PAINTBUFFER_SIZE	2048
portable_samplepair_t paintbuffer[PAINTBUFFER_SIZE];
int		snd_scaletable[32][256];
int 	*snd_p, snd_linear_count, snd_vol;
short	*snd_out;

void S_WriteLinearBlastStereo16 (void);

#if !(defined __linux__ && defined __i386__)
#if	!id386

void S_WriteLinearBlastStereo16 (void)
{
	int		i;
	int		val;

	for (i=0 ; i<snd_linear_count ; i+=2)
	{
		val = snd_p[i]>>8;
		if (val > 0x7fff)
			snd_out[i] = 0x7fff;
		else if (val < (short)0x8000)
			snd_out[i] = (short)0x8000;
		else
			snd_out[i] = val;

		val = snd_p[i+1]>>8;
		if (val > 0x7fff)
			snd_out[i+1] = 0x7fff;
		else if (val < (short)0x8000)
			snd_out[i+1] = (short)0x8000;
		else
			snd_out[i+1] = val;
	}
}
#else
__declspec( naked ) void S_WriteLinearBlastStereo16 (void)
{
	__asm {

 push edi
 push ebx
 mov ecx,ds:dword ptr[snd_linear_count]
 mov ebx,ds:dword ptr[snd_p]
 mov edi,ds:dword ptr[snd_out]
LWLBLoopTop:
 mov eax,ds:dword ptr[-8+ebx+ecx*4]
 sar eax,8
 cmp eax,07FFFh
 jg LClampHigh
 cmp eax,0FFFF8000h
 jnl LClampDone
 mov eax,0FFFF8000h
 jmp LClampDone
LClampHigh:
 mov eax,07FFFh
LClampDone:
 mov edx,ds:dword ptr[-4+ebx+ecx*4]
 sar edx,8
 cmp edx,07FFFh
 jg LClampHigh2
 cmp edx,0FFFF8000h
 jnl LClampDone2
 mov edx,0FFFF8000h
 jmp LClampDone2
LClampHigh2:
 mov edx,07FFFh
LClampDone2:
 shl edx,16
 and eax,0FFFFh
 or edx,eax
 mov ds:dword ptr[-4+edi+ecx*2],edx
 sub ecx,2
 jnz LWLBLoopTop
 pop ebx
 pop edi
 ret
	}
}

#endif
#endif


/*
=============================================================================
BDGN 3.35: MMX mixing (s_mmx).  Bit-exact copies of the C code above / below, 2 frames per step.
S_WriteLinearBlastStereo16_MMX: >>8 and the clamp to 16 bit are one psrad + packssdw.
S_PaintChannelFrom16_MMX: (data*vol)>>8 is split as data*(vol>>8) + ((data*(vol&255))>>8), which gives the
same result as the 32-bit C expression, and runs with pmaddwd.  BDGN 3.41: the 8-bit path has an MMX version too.
=============================================================================
*/
cvar_t	*s_mmx;
static int	snd_use_mmx;
cvar_t	*s_amd_mix;
static int	snd_use_amd;

/* v3.51M development: reuse the guarded, cached engine detector. */
static int Snd_CpuHasMMX (void)
{
	return Q3E_CPUHasMMX ();
}

static void S_WriteLinearBlastStereo16_MMX (const int *input, short *output, int n)
{
	int		i;
	int		val;

	for (i = 0 ; i + 4 <= n ; i += 4)
	{
		__m64 a = *(const __m64 *)(input + i);
		__m64 b = *(const __m64 *)(input + i + 2);

		a = _mm_srai_pi32 (a, 8);
		b = _mm_srai_pi32 (b, 8);
		*(__m64 *)(output + i) = _mm_packs_pi32 (a, b);
	}
	_mm_empty ();

	for ( ; i < n ; i++)
	{
		val = input[i]>>8;
		if (val > 0x7fff)
			output[i] = 0x7fff;
		else if (val < (short)0x8000)
			output[i] = (short)0x8000;
		else
			output[i] = (short)val;
	}
}

void S_PaintChannelFrom16_MMX (channel_t *ch, sfxcache_t *sc, int count, int offset)
{
	int		leftvol, rightvol;
	signed short	*sfx;
	int		i;
	portable_samplepair_t	*samp;

	leftvol = ch->leftvol*snd_vol;
	rightvol = ch->rightvol*snd_vol;
	sfx = (signed short *)sc->data + ch->pos;
	samp = &paintbuffer[offset];

	if (leftvol < 0 || rightvol < 0 || leftvol > 0xff00 || rightvol > 0xff00)
	{	// outside what the exact split below covers (255*256), use the plain code
		int data, left, right;

		for (i=0 ; i<count ; i++, samp++)
		{
			data = sfx[i];
			left = (data * leftvol)>>8;
			right = (data * rightvol)>>8;
			samp->left += left;
			samp->right += right;
		}
		ch->pos += count;
		return;
	}

	{
		// multipliers as words [v,0,v,0]: pmaddwd with A = [s0,s0,s1,s1] gives [s0*v, s1*v]
		__m64 mhl = _mm_set_pi16 (0, (short)(leftvol >> 8), 0, (short)(leftvol >> 8));
		__m64 mll = _mm_set_pi16 (0, (short)(leftvol & 255), 0, (short)(leftvol & 255));
		__m64 mhr = _mm_set_pi16 (0, (short)(rightvol >> 8), 0, (short)(rightvol >> 8));
		__m64 mlr = _mm_set_pi16 (0, (short)(rightvol & 255), 0, (short)(rightvol & 255));

		for (i = 0 ; i + 2 <= count ; i += 2, samp += 2)
		{
			int two = *(const int *)(sfx + i);			// s0 | s1<<16
			__m64 a = _mm_cvtsi32_si64 (two);
			__m64 x, y, xl, yl, f0, f1;

			a = _mm_unpacklo_pi16 (a, a);					// [s0,s0,s1,s1]
			x = _mm_madd_pi16 (a, mhl);					// [s0*hL, s1*hL]
			xl = _mm_srai_pi32 (_mm_madd_pi16 (a, mll), 8);	// [(s0*lL)>>8, (s1*lL)>>8]
			x = _mm_add_pi32 (x, xl);
			y = _mm_madd_pi16 (a, mhr);					// [s0*hR, s1*hR]
			yl = _mm_srai_pi32 (_mm_madd_pi16 (a, mlr), 8);
			y = _mm_add_pi32 (y, yl);
			f0 = _mm_unpacklo_pi32 (x, y);				// [L0, R0]
			f1 = _mm_unpackhi_pi32 (x, y);				// [L1, R1]
			*(__m64 *)(&samp[0]) = _mm_add_pi32 (*(const __m64 *)(&samp[0]), f0);
			*(__m64 *)(&samp[1]) = _mm_add_pi32 (*(const __m64 *)(&samp[1]), f1);
		}
		_mm_empty ();

		for ( ; i < count ; i++, samp++)
		{
			int data = sfx[i];

			samp->left += (data * leftvol)>>8;
			samp->right += (data * rightvol)>>8;
		}
	}

	ch->pos += count;
}

/*
BDGN 3.41: S_PaintChannelFrom8_MMX.  Same result as S_PaintChannelFrom8: the table entry is
(signed char)data * scale (see S_InitScaletable), so the table lookup is replaced by data*scale, split as
data*(scale>>8)<<8 + data*(scale&255) so that both products fit the 16 bit pmaddwd.  Two samples per step.
*/
void S_PaintChannelFrom8_MMX (channel_t *ch, sfxcache_t *sc, int count, int offset)
{
	int		lscale, rscale, i;
	signed char	*sfx;
	portable_samplepair_t	*samp;

	if (ch->leftvol > 255)
		ch->leftvol = 255;
	if (ch->rightvol > 255)
		ch->rightvol = 255;

	lscale = snd_scaletable[ ch->leftvol >> 3 ][1];		// = scale of that volume step
	rscale = snd_scaletable[ ch->rightvol >> 3 ][1];
	sfx = (signed char *)sc->data + ch->pos;
	samp = &paintbuffer[offset];

	if (lscale < 0 || rscale < 0 || lscale > 0xffff || rscale > 0xffff)
	{	// (s_volume above 1) outside what the split below covers: the plain table code
		int *ls = snd_scaletable[ ch->leftvol >> 3 ];
		int *rs = snd_scaletable[ ch->rightvol >> 3 ];

		for (i=0 ; i<count ; i++, samp++)
		{
			int data = sfx[i];

			samp->left += ls[data & 255];
			samp->right += rs[data & 255];
		}
		ch->pos += count;
		return;
	}

	{
		__m64 mhl = _mm_set_pi16 (0, (short)(lscale >> 8), 0, (short)(lscale >> 8));
		__m64 mll = _mm_set_pi16 (0, (short)(lscale & 255), 0, (short)(lscale & 255));
		__m64 mhr = _mm_set_pi16 (0, (short)(rscale >> 8), 0, (short)(rscale >> 8));
		__m64 mlr = _mm_set_pi16 (0, (short)(rscale & 255), 0, (short)(rscale & 255));

		for (i = 0 ; i + 2 <= count ; i += 2, samp += 2)
		{
			int two = *(const unsigned short *)(sfx + i);		// s0 | s1<<8
			__m64 a = _mm_cvtsi32_si64 (two);
			__m64 x, y, f0, f1;

			a = _mm_unpacklo_pi8 (a, a);					// bytes s0 s0 s1 s1
			a = _mm_srai_pi16 (a, 8);						// words s0, s1 (sign extended)
			a = _mm_unpacklo_pi16 (a, a);					// [s0,s0,s1,s1]
			x = _mm_add_pi32 (_mm_slli_pi32 (_mm_madd_pi16 (a, mhl), 8), _mm_madd_pi16 (a, mll));
			y = _mm_add_pi32 (_mm_slli_pi32 (_mm_madd_pi16 (a, mhr), 8), _mm_madd_pi16 (a, mlr));
			f0 = _mm_unpacklo_pi32 (x, y);				// [L0, R0]
			f1 = _mm_unpackhi_pi32 (x, y);				// [L1, R1]
			*(__m64 *)(&samp[0]) = _mm_add_pi32 (*(const __m64 *)(&samp[0]), f0);
			*(__m64 *)(&samp[1]) = _mm_add_pi32 (*(const __m64 *)(&samp[1]), f1);
		}
		_mm_empty ();

		for ( ; i < count ; i++, samp++)
		{
			int data = sfx[i];

			samp->left += data * lscale;
			samp->right += data * rscale;
		}
	}

	ch->pos += count;
}

/*
BDGN 3.50: AMD's sound mixing (s_amd_mix, used before s_mmx when on).  The kernels are in qcommon/q3e_3dnow.c;
the channel handling here is id's.  8 bit is bit-identical to id, 16 bit at most one step off.
*/
void S_PaintChannelFrom16 (channel_t *ch, sfxcache_t *sc, int count, int offset);	// id's C version (below), the fallback

static void S_PaintChannelFrom8_AMD (channel_t *ch, sfxcache_t *sc, int count, int offset)
{
	if (ch->leftvol > 255)
		ch->leftvol = 255;
	if (ch->rightvol > 255)
		ch->rightvol = 255;

	Q3E_PaintFrom8 (&paintbuffer[offset].left, sc->data + ch->pos, count,
		snd_scaletable[ ch->leftvol >> 3 ], snd_scaletable[ ch->rightvol >> 3 ]);
	ch->pos += count;
}

static void S_PaintChannelFrom16_AMD (channel_t *ch, sfxcache_t *sc, int count, int offset)
{
	if (!Q3E_PaintFrom16 (&paintbuffer[offset].left, (short *)sc->data + ch->pos, count,
		ch->leftvol * snd_vol, ch->rightvol * snd_vol))
	{	// volume above what the kernel covers (s_volume > 1): id's code
		S_PaintChannelFrom16 (ch, sc, count, offset);
		return;
	}
	ch->pos += count;
}

#ifdef _WIN32
static HANDLE pcm_thread, pcm_go, pcm_done;
static qboolean pcm_quit, pcm_failed;
static const int *pcm_input;
static short *pcm_output;
static int pcm_count, pcm_mmx;
static unsigned pcm_batches;

static void S_WritePcmRange (const int *input, short *output, int count, int mmx)
{
 int i, value;
 if (mmx) {
  S_WriteLinearBlastStereo16_MMX (input, output, count);
  return;
 }
 for (i=0; i<count; i++) {
  value=input[i]>>8;
  output[i]=(short)(value>32767?32767:value<-32768?-32768:value);
 }
}

static unsigned __stdcall S_PcmWorker (void *unused)
{
 (void)unused;
 while (WaitForSingleObject(pcm_go,INFINITE)==WAIT_OBJECT_0 && !pcm_quit) {
  S_WritePcmRange(pcm_input,pcm_output,pcm_count,pcm_mmx);
  SetEvent(pcm_done);
 }
 return 0;
}

void S_PcmWorkerShutdown (void)
{
 if (pcm_thread) {
  pcm_quit=true;
  SetEvent(pcm_go);
  WaitForSingleObject(pcm_thread,INFINITE);
  CloseHandle(pcm_thread);
  pcm_thread=NULL;
 }
 if (pcm_go) CloseHandle(pcm_go);
 if (pcm_done) CloseHandle(pcm_done);
 pcm_go=pcm_done=NULL;
 pcm_failed=false;
}

static qboolean S_PcmParallel (void)
{
 int system, process, split;
 unsigned id;
 static cvar_t *smp;
 int mode;
 if (!smp) smp=Cvar_Get("sw_smp","0",CVAR_ARCHIVE);
 if (snd_linear_count<256 || pcm_failed) return false;
 if (!Q_stricmp(Cvar_VariableString("vid_ref"),"soft")) {
  mode=(int)smp->value;
  if ((mode!=SW_SMP_AUDIO && mode!=SW_SMP_ADAPTIVE_AUDIO) ||
      Cvar_VariableValue("sw_smp_modes")!=SW_SMP_COUNT) return false;
 } else if ((int)Cvar_VariableValue("s_gl_audio_worker")!=AUDIO_WORKER_PCM) return false;
 if (!pcm_thread) {
  Sys_ProcessorCounts(&system,&process);
  if (process<2) return false;
  pcm_quit=false;
  pcm_go=CreateEvent(NULL,FALSE,FALSE,NULL);
  pcm_done=CreateEvent(NULL,FALSE,FALSE,NULL);
  if (pcm_go && pcm_done)
   pcm_thread=(HANDLE)_beginthreadex(NULL,0,S_PcmWorker,NULL,0,&id);
  if (!pcm_thread) {
   S_PcmWorkerShutdown();
   pcm_failed=true;
   return false;
  }
 }
 split=(snd_linear_count/2)&~3;
 pcm_input=snd_p+split;
 pcm_output=snd_out+split;
 pcm_count=snd_linear_count-split;
 pcm_mmx=snd_use_mmx;
 if (!SetEvent(pcm_go)) return false;
 S_WritePcmRange(snd_p,snd_out,split,snd_use_mmx);
 WaitForSingleObject(pcm_done,INFINITE);
 pcm_batches++;
 return true;
}

unsigned S_PcmWorkerBatches (void) { return pcm_batches; }
#endif

void S_TransferStereo16 (unsigned long *pbuf, int endtime)
{
	int		lpos;
	int		lpaintedtime;
	
	snd_p = (int *) paintbuffer;
	lpaintedtime = paintedtime;

	while (lpaintedtime < endtime)
	{
	// handle recirculating buffer issues
		lpos = lpaintedtime & ((dma.samples>>1)-1);

		snd_out = (short *) pbuf + (lpos<<1);

		snd_linear_count = (dma.samples>>1) - lpos;
		if (lpaintedtime + snd_linear_count > endtime)
			snd_linear_count = endtime - lpaintedtime;

		snd_linear_count <<= 1;

	// write a linear blast of samples
#ifdef _WIN32
		if (!S_PcmParallel ())
#endif
		{
			if (snd_use_mmx)
				S_WriteLinearBlastStereo16_MMX (snd_p,snd_out,snd_linear_count);
			else
				S_WriteLinearBlastStereo16 ();
		}

		snd_p += snd_linear_count;
		lpaintedtime += (snd_linear_count>>1);
	}
}

/*
===================
S_TransferPaintBuffer

===================
*/
void S_TransferPaintBuffer(int endtime)
{
	int 	out_idx;
	int 	count;
	int 	out_mask;
	int 	*p;
	int 	step;
	int		val;
	unsigned long *pbuf;

	pbuf = (unsigned long *)dma.buffer;

	if (s_testsound->value)
	{
		int		i;
		int		count;

		// write a fixed sine wave
		count = (endtime - paintedtime);
		for (i=0 ; i<count ; i++)
			paintbuffer[i].left = paintbuffer[i].right = sin((paintedtime+i)*0.1)*20000*256;
	}


	if (dma.samplebits == 16 && dma.channels == 2)
	{	// optimized case
		S_TransferStereo16 (pbuf, endtime);
	}
	else
	{	// general case
		p = (int *) paintbuffer;
		count = (endtime - paintedtime) * dma.channels;
		out_mask = dma.samples - 1; 
		out_idx = paintedtime * dma.channels & out_mask;
		step = 3 - dma.channels;

		if (dma.samplebits == 16)
		{
			short *out = (short *) pbuf;
			while (count--)
			{
				val = *p >> 8;
				p+= step;
				if (val > 0x7fff)
					val = 0x7fff;
				else if (val < (short)0x8000)
					val = (short)0x8000;
				out[out_idx] = val;
				out_idx = (out_idx + 1) & out_mask;
			}
		}
		else if (dma.samplebits == 8)
		{
			unsigned char *out = (unsigned char *) pbuf;
			while (count--)
			{
				val = *p >> 8;
				p+= step;
				if (val > 0x7fff)
					val = 0x7fff;
				else if (val < (short)0x8000)
					val = (short)0x8000;
				out[out_idx] = (val>>8) + 128;
				out_idx = (out_idx + 1) & out_mask;
			}
		}
	}
}


/*
===============================================================================

CHANNEL MIXING

===============================================================================
*/

void S_PaintChannelFrom8 (channel_t *ch, sfxcache_t *sc, int endtime, int offset);
void S_PaintChannelFrom16 (channel_t *ch, sfxcache_t *sc, int endtime, int offset);

void S_PaintChannels(int endtime)
{
	int 	i;
	int 	end;
	channel_t *ch;
	sfxcache_t	*sc;
	int		ltime, count;
	playsound_t	*ps;

	snd_vol = s_volume->value*256;
	snd_use_mmx = (s_mmx && s_mmx->value && Snd_CpuHasMMX ()) ? 1 : 0;
	snd_use_amd = (s_amd_mix && s_amd_mix->value && Q3E_CPUHas3DNow ()) ? 1 : 0;	// BDGN 3.50

//Com_Printf ("%i to %i\n", paintedtime, endtime);
	while (paintedtime < endtime)
	{
	// if paintbuffer is smaller than DMA buffer
		end = endtime;
		if (endtime - paintedtime > PAINTBUFFER_SIZE)
			end = paintedtime + PAINTBUFFER_SIZE;

		// start any playsounds
		while (1)
		{
			ps = s_pendingplays.next;
			if (ps == &s_pendingplays)
				break;	// no more pending sounds
			if (ps->begin <= paintedtime)
			{
				S_IssuePlaysound (ps);
				continue;
			}

			if (ps->begin < end)
				end = ps->begin;		// stop here
			break;
		}

	// clear the paint buffer
		if (s_rawend < paintedtime)
		{
//			Com_Printf ("clear\n");
			memset(paintbuffer, 0, (end - paintedtime) * sizeof(portable_samplepair_t));
		}
		else
		{	// copy from the streaming sound source
			int		s;
			int		stop;

			stop = (end < s_rawend) ? end : s_rawend;

			for (i=paintedtime ; i<stop ; i++)
			{
				s = i&(MAX_RAW_SAMPLES-1);
				paintbuffer[i-paintedtime] = s_rawsamples[s];
			}
//		if (i != end)
//			Com_Printf ("partial stream\n");
//		else
//			Com_Printf ("full stream\n");
			for ( ; i<end ; i++)
			{
				paintbuffer[i-paintedtime].left =
				paintbuffer[i-paintedtime].right = 0;
			}
		}


	// paint in the channels.
		ch = channels;
		for (i=0; i<MAX_CHANNELS ; i++, ch++)
		{
			ltime = paintedtime;
		
			while (ltime < end)
			{
				if (!ch->sfx || (!ch->leftvol && !ch->rightvol) )
					break;

				// max painting is to the end of the buffer
				count = end - ltime;

				// might be stopped by running out of data
				if (ch->end - ltime < count)
					count = ch->end - ltime;
		
				sc = S_LoadSound (ch->sfx);
				if (!sc)
					break;

				if (count > 0 && ch->sfx)
				{	
					if (sc->width == 1)// FIXME; 8 bit asm is wrong now
					{
						if (snd_use_amd)
							S_PaintChannelFrom8_AMD(ch, sc, count,  ltime - paintedtime);	// BDGN 3.50
						else if (snd_use_mmx)
							S_PaintChannelFrom8_MMX(ch, sc, count,  ltime - paintedtime);	// BDGN 3.41
						else
							S_PaintChannelFrom8(ch, sc, count,  ltime - paintedtime);
					}
					else if (snd_use_amd)
						S_PaintChannelFrom16_AMD(ch, sc, count, ltime - paintedtime);	// BDGN 3.50
					else if (snd_use_mmx)
						S_PaintChannelFrom16_MMX(ch, sc, count, ltime - paintedtime);
					else
						S_PaintChannelFrom16(ch, sc, count, ltime - paintedtime);
	
					ltime += count;
				}

			// if at end of loop, restart
				if (ltime >= ch->end)
				{
					if (ch->autosound)
					{	// autolooping sounds always go back to start
						ch->pos = 0;
						ch->end = ltime + sc->length;
					}
					else if (sc->loopstart >= 0)
					{
						ch->pos = sc->loopstart;
						ch->end = ltime + sc->length - ch->pos;
					}
					else				
					{	// channel just stopped
						ch->sfx = NULL;
					}
				}
			}
															  
		}

		PCSPK_Paint (paintbuffer, end - paintedtime, dma.speed);	/* BDGN 3.58l: PC speaker through the sound card */

	// transfer out according to DMA format
		S_TransferPaintBuffer(end);
		paintedtime = end;
	}
}

void S_InitScaletable (void)
{
	int		i, j;
	int		scale;

	s_volume->modified = false;
	for (i=0 ; i<32 ; i++)
	{
		scale = i * 8 * 256 * s_volume->value;
		for (j=0 ; j<256 ; j++)
			snd_scaletable[i][j] = ((signed char)j) * scale;
	}
}


#if !(defined __linux__ && defined __i386__)
#if	!id386

void S_PaintChannelFrom8 (channel_t *ch, sfxcache_t *sc, int count, int offset)
{
	int 	data;
	int		*lscale, *rscale;
	unsigned char *sfx;
	int		i;
	portable_samplepair_t	*samp;

	if (ch->leftvol > 255)
		ch->leftvol = 255;
	if (ch->rightvol > 255)
		ch->rightvol = 255;
		
	//ZOID-- >>11 has been changed to >>3, >>11 didn't make much sense
	//as it would always be zero.
	lscale = snd_scaletable[ ch->leftvol >> 3];
	rscale = snd_scaletable[ ch->rightvol >> 3];
	sfx = (signed char *)sc->data + ch->pos;

	samp = &paintbuffer[offset];

	for (i=0 ; i<count ; i++, samp++)
	{
		data = sfx[i];
		samp->left += lscale[data];
		samp->right += rscale[data];
	}
	
	ch->pos += count;
}

#else

__declspec( naked ) void S_PaintChannelFrom8 (channel_t *ch, sfxcache_t *sc, int count, int offset)
{
	__asm {
 push esi
 push edi
 push ebx
 push ebp
 mov ebx,ds:dword ptr[4+16+esp]
 mov esi,ds:dword ptr[8+16+esp]
 mov eax,ds:dword ptr[4+ebx]
 mov edx,ds:dword ptr[8+ebx]
 cmp eax,255
 jna LLeftSet
 mov eax,255
LLeftSet:
 cmp edx,255
 jna LRightSet
 mov edx,255
LRightSet:
 and eax,0F8h
 add esi,20
 and edx,0F8h
 mov edi,ds:dword ptr[16+ebx]
 mov ecx,ds:dword ptr[12+16+esp]
 add esi,edi
 shl eax,7
 add edi,ecx
 shl edx,7
 mov ds:dword ptr[16+ebx],edi
 add eax,offset snd_scaletable
 add edx,offset snd_scaletable
 sub ebx,ebx
 mov bl,ds:byte ptr[-1+esi+ecx*1]
 test ecx,1
 jz LMix8Loop
 mov edi,ds:dword ptr[eax+ebx*4]
 mov ebp,ds:dword ptr[edx+ebx*4]
 add edi,ds:dword ptr[paintbuffer+0-8+ecx*8]
 add ebp,ds:dword ptr[paintbuffer+4-8+ecx*8]
 mov ds:dword ptr[paintbuffer+0-8+ecx*8],edi
 mov ds:dword ptr[paintbuffer+4-8+ecx*8],ebp
 mov bl,ds:byte ptr[-2+esi+ecx*1]
 dec ecx
 jz LDone
LMix8Loop:
 mov edi,ds:dword ptr[eax+ebx*4]
 mov ebp,ds:dword ptr[edx+ebx*4]
 add edi,ds:dword ptr[paintbuffer+0-8+ecx*8]
 add ebp,ds:dword ptr[paintbuffer+4-8+ecx*8]
 mov bl,ds:byte ptr[-2+esi+ecx*1]
 mov ds:dword ptr[paintbuffer+0-8+ecx*8],edi
 mov ds:dword ptr[paintbuffer+4-8+ecx*8],ebp
 mov edi,ds:dword ptr[eax+ebx*4]
 mov ebp,ds:dword ptr[edx+ebx*4]
 mov bl,ds:byte ptr[-3+esi+ecx*1]
 add edi,ds:dword ptr[paintbuffer+0-8*2+ecx*8]
 add ebp,ds:dword ptr[paintbuffer+4-8*2+ecx*8]
 mov ds:dword ptr[paintbuffer+0-8*2+ecx*8],edi
 mov ds:dword ptr[paintbuffer+4-8*2+ecx*8],ebp
 sub ecx,2
 jnz LMix8Loop
LDone:
 pop ebp
 pop ebx
 pop edi
 pop esi
 ret
	}
}

#endif
#endif

void S_PaintChannelFrom16 (channel_t *ch, sfxcache_t *sc, int count, int offset)
{
	int data;
	int left, right;
	int leftvol, rightvol;
	signed short *sfx;
	int	i;
	portable_samplepair_t	*samp;

	leftvol = ch->leftvol*snd_vol;
	rightvol = ch->rightvol*snd_vol;
	sfx = (signed short *)sc->data + ch->pos;

	samp = &paintbuffer[offset];
	for (i=0 ; i<count ; i++, samp++)
	{
		data = sfx[i];
		left = (data * leftvol)>>8;
		right = (data * rightvol)>>8;
		samp->left += left;
		samp->right += right;
	}

	ch->pos += count;
}

