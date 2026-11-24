/* Streaming music sources for WinMM playback: PCM WAV read from disk and MP3 decoded frame by frame.
   No Windows code here, so it is tested on its own (test/music_stream_test.c). */
#ifndef SND_MUSICSTREAM_H
#define SND_MUSICSTREAM_H

#include <stdio.h>
#include "minimp3.h"

#define MUSICSTREAM_MP3_INPUT	32768	/* MP3 input window; refilled when half used */

typedef enum
{
	MUSICSTREAM_OK,
	MUSICSTREAM_ERR_OPEN,		/* file could not be opened */
	MUSICSTREAM_ERR_FORMAT,		/* not a RIFF/WAVE file, or no usable fmt/data chunk */
	MUSICSTREAM_ERR_CODEC,		/* WAV that is not 8/16-bit PCM: let MCI try it */
	MUSICSTREAM_ERR_NODATA		/* MP3 without a decodable frame */
} musicstream_error_t;

typedef struct
{
	FILE			*file;
	int				is_mp3;
	int				looping;
	int				ended;			/* no more data will come (never set while looping, unless the source is empty) */
	int				channels, rate, bits, align;	/* output PCM format; MP3 is always 16-bit */
	long			data_start;		/* WAV: first byte of the data chunk; MP3: first byte after an ID3v2 tag */
	unsigned long	data_length;	/* WAV: data bytes, a multiple of align */
	unsigned long	data_pos;		/* WAV: bytes read from the data chunk */
	/* MP3 decoder state */
	mp3dec_t		decoder;
	unsigned char	input[MUSICSTREAM_MP3_INPUT];
	int				input_length, input_pos, input_eof;
	short			pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	int				pcm_length, pcm_pos;	/* bytes */
} musicstream_t;

musicstream_error_t MusicStream_OpenWav (musicstream_t *stream, const char *path, int looping);
musicstream_error_t MusicStream_OpenMP3 (musicstream_t *stream, const char *path, int looping);
/* Fills out with up to bytes of PCM (a multiple of align), scaled by volume (clamped to 0..1).
   Returns the bytes written; less than asked only when the stream has ended. */
int MusicStream_Read (musicstream_t *stream, unsigned char *out, int bytes, float volume);
void MusicStream_Close (musicstream_t *stream);
const char *MusicStream_ErrorText (musicstream_error_t error);

#endif
