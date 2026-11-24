/* Streaming music sources: PCM WAV read from disk and MP3 decoded frame by frame, so a track never has to
   be held in memory whole. See snd_musicstream.h. */
#include <stdlib.h>
#include <string.h>
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "snd_musicstream.h"

static unsigned long Stream_LE32 (const unsigned char *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned Stream_LE16 (const unsigned char *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

const char *MusicStream_ErrorText (musicstream_error_t error)
{
	switch (error)
	{
	case MUSICSTREAM_OK:			return "no error";
	case MUSICSTREAM_ERR_OPEN:		return "file could not be opened";
	case MUSICSTREAM_ERR_FORMAT:	return "not a usable WAV file";
	case MUSICSTREAM_ERR_CODEC:		return "WAV is not 8 or 16-bit PCM";
	case MUSICSTREAM_ERR_NODATA:	return "no MP3 audio frames found";
	}
	return "unknown error";
}

void MusicStream_Close (musicstream_t *stream)
{
	if (stream->file)
		fclose (stream->file);
	stream->file = NULL;
	stream->ended = 1;
}

musicstream_error_t MusicStream_OpenWav (musicstream_t *stream, const char *path, int looping)
{
	unsigned char header[12], chunk[8], fmt[16];
	long file_length, offset, remaining;
	unsigned long chunk_length;
	int have_fmt = 0, have_data = 0;
	unsigned format_tag;

	memset (stream, 0, sizeof(*stream));
	stream->ended = 1;
	stream->file = fopen (path, "rb");
	if (!stream->file)
		return MUSICSTREAM_ERR_OPEN;
	fseek (stream->file, 0, SEEK_END);
	file_length = ftell (stream->file);
	fseek (stream->file, 0, SEEK_SET);
	if (file_length < 44 || fread (header, 1, 12, stream->file) != 12 || memcmp (header, "RIFF", 4) || memcmp (header + 8, "WAVE", 4))
	{
		MusicStream_Close (stream);
		return MUSICSTREAM_ERR_FORMAT;
	}
	/* walk the chunks; the first fmt and the first data chunk count */
	for (offset = 12; offset + 8 <= file_length && !(have_fmt && have_data); )
	{
		if (fseek (stream->file, offset, SEEK_SET) || fread (chunk, 1, 8, stream->file) != 8)
			break;
		chunk_length = Stream_LE32 (chunk + 4);
		remaining = file_length - offset - 8;
		if (!memcmp (chunk, "fmt ", 4) && !have_fmt)
		{
			if (chunk_length < 16 || chunk_length > (unsigned long)remaining || fread (fmt, 1, 16, stream->file) != 16)
				break;
			have_fmt = 1;
		}
		else if (!memcmp (chunk, "data", 4) && !have_data)
		{
			if (chunk_length > (unsigned long)remaining)
				chunk_length = (unsigned long)remaining;	/* truncated file: play what is there */
			stream->data_start = offset + 8;
			stream->data_length = chunk_length;
			have_data = 1;
		}
		if (chunk_length > (unsigned long)remaining)
			break;
		offset += 8 + (long)chunk_length + (long)(chunk_length & 1);
	}
	if (!have_fmt || !have_data)
	{
		MusicStream_Close (stream);
		return MUSICSTREAM_ERR_FORMAT;
	}
	format_tag = Stream_LE16 (fmt);
	stream->channels = (int)Stream_LE16 (fmt + 2);
	stream->rate = (int)Stream_LE32 (fmt + 4);
	stream->align = (int)Stream_LE16 (fmt + 12);
	stream->bits = (int)Stream_LE16 (fmt + 14);
	if (format_tag != 1 || (stream->bits != 8 && stream->bits != 16) || stream->channels < 1 || stream->channels > 2
		|| stream->align != stream->channels * stream->bits / 8 || stream->rate < 1000 || stream->rate > 96000)
	{
		MusicStream_Close (stream);
		return MUSICSTREAM_ERR_CODEC;
	}
	stream->data_length -= stream->data_length % (unsigned long)stream->align;
	if (!stream->data_length || fseek (stream->file, stream->data_start, SEEK_SET))
	{
		MusicStream_Close (stream);
		return MUSICSTREAM_ERR_FORMAT;
	}
	stream->looping = looping;
	stream->ended = 0;
	return MUSICSTREAM_OK;
}

/* Moves unread input to the front of the window and tops it up from the file. */
static void Stream_FillInput (musicstream_t *stream)
{
	size_t got;
	if (stream->input_pos > 0)
	{
		stream->input_length -= stream->input_pos;
		memmove (stream->input, stream->input + stream->input_pos, stream->input_length);
		stream->input_pos = 0;
	}
	if (stream->input_eof || stream->input_length >= MUSICSTREAM_MP3_INPUT)
		return;
	got = fread (stream->input + stream->input_length, 1, MUSICSTREAM_MP3_INPUT - stream->input_length, stream->file);
	stream->input_length += (int)got;
	if (stream->input_length < MUSICSTREAM_MP3_INPUT)
		stream->input_eof = 1;
}

static int Stream_RewindMP3 (musicstream_t *stream)
{
	if (fseek (stream->file, stream->data_start, SEEK_SET))
		return 0;
	mp3dec_init (&stream->decoder);
	stream->input_length = stream->input_pos = stream->input_eof = 0;
	stream->pcm_length = stream->pcm_pos = 0;
	return 1;
}

/* Decodes the next frame into stream->pcm. Returns 0 at the end of the file or on a format change. */
static int Stream_DecodeMP3 (musicstream_t *stream)
{
	mp3dec_frame_info_t info;
	int samples, left;
	for (;;)
	{
		if (stream->input_pos >= MUSICSTREAM_MP3_INPUT / 2 || (stream->input_length < MUSICSTREAM_MP3_INPUT && !stream->input_eof))
			Stream_FillInput (stream);
		left = stream->input_length - stream->input_pos;
		if (left <= 0)
			return 0;
		memset (&info, 0, sizeof(info));
		samples = mp3dec_decode_frame (&stream->decoder, stream->input + stream->input_pos, left, stream->pcm, &info);
		if (info.frame_bytes <= 0)
		{
			/* the window ends inside a frame */
			if (stream->input_eof)
				return 0;
			if (stream->input_pos == 0 && stream->input_length >= MUSICSTREAM_MP3_INPUT)
				stream->input_pos = 1;	/* cannot happen with real frames (max 2881 bytes); never stall */
			Stream_FillInput (stream);
			continue;
		}
		if (info.frame_bytes > left)
			return 0;
		stream->input_pos += info.frame_bytes;
		if (samples <= 0)
			continue;	/* skipped tag or junk */
		if (info.channels < 1 || info.channels > 2 || info.hz < 8000 || info.hz > 48000)
			return 0;
		if (stream->channels && (info.channels != stream->channels || info.hz != stream->rate))
			return 0;	/* the output device cannot change format mid-track */
		stream->channels = info.channels;
		stream->rate = info.hz;
		stream->pcm_length = samples * info.channels * (int)sizeof(short);
		stream->pcm_pos = 0;
		return 1;
	}
}

musicstream_error_t MusicStream_OpenMP3 (musicstream_t *stream, const char *path, int looping)
{
	unsigned char id3[10];
	long tag = 0;

	memset (stream, 0, sizeof(*stream));
	stream->ended = 1;
	stream->is_mp3 = 1;
	stream->file = fopen (path, "rb");
	if (!stream->file)
		return MUSICSTREAM_ERR_OPEN;
	/* skip an ID3v2 tag (it can hold large pictures) so loops restart at the audio */
	if (fread (id3, 1, 10, stream->file) == 10 && !memcmp (id3, "ID3", 3)
		&& !((id3[6] | id3[7] | id3[8] | id3[9]) & 0x80))
	{
		tag = 10 + (((long)id3[6] << 21) | ((long)id3[7] << 14) | ((long)id3[8] << 7) | (long)id3[9]);
		if (id3[5] & 0x10)
			tag += 10;	/* footer */
	}
	stream->data_start = tag;
	if (!Stream_RewindMP3 (stream) || !Stream_DecodeMP3 (stream))
	{
		MusicStream_Close (stream);
		return MUSICSTREAM_ERR_NODATA;
	}
	stream->bits = 16;
	stream->align = stream->channels * 2;
	stream->looping = looping;
	stream->ended = 0;
	return MUSICSTREAM_OK;
}

/* Raw PCM from the source, looping as needed. */
static int Stream_ReadRaw (musicstream_t *stream, unsigned char *out, int bytes)
{
	int done = 0, chunk, empty_rewinds = 0;
	size_t got;
	while (done < bytes && !stream->ended)
	{
		if (stream->is_mp3)
		{
			if (stream->pcm_pos >= stream->pcm_length && !Stream_DecodeMP3 (stream))
				chunk = 0;
			else
			{
				chunk = stream->pcm_length - stream->pcm_pos;
				if (chunk > bytes - done)
					chunk = bytes - done;
				memcpy (out + done, (unsigned char *)stream->pcm + stream->pcm_pos, chunk);
				stream->pcm_pos += chunk;
			}
		}
		else
		{
			chunk = bytes - done;
			if ((unsigned long)chunk > stream->data_length - stream->data_pos)
				chunk = (int)(stream->data_length - stream->data_pos);
			got = chunk ? fread (out + done, 1, chunk, stream->file) : 0;
			chunk = (int)got;
			stream->data_pos += got;
			if (!got)
				stream->data_pos = stream->data_length;	/* read error counts as the end */
		}
		if (chunk > 0)
		{
			done += chunk;
			empty_rewinds = 0;
			continue;
		}
		/* end of the source */
		if (!stream->looping || ++empty_rewinds > 1)
		{
			stream->ended = 1;
			break;
		}
		if (stream->is_mp3)
		{
			if (!Stream_RewindMP3 (stream))
				stream->ended = 1;
		}
		else
		{
			stream->data_pos = 0;
			if (fseek (stream->file, stream->data_start, SEEK_SET))
				stream->ended = 1;
		}
	}
	return done;
}

int MusicStream_Read (musicstream_t *stream, unsigned char *out, int bytes, float volume)
{
	int done, i, gain;
	if (!stream->file || bytes <= 0 || stream->align <= 0)
		return 0;
	bytes -= bytes % stream->align;
	done = Stream_ReadRaw (stream, out, bytes);
	done -= done % stream->align;
	/* volume in 16.16 fixed point: one float conversion per call, not one per sample (a slow _ftol call
	   on VS2005/x87). sample * gain / 65536 truncates toward zero like (int)(sample * volume) and is
	   within 1 of it; sample * gain fits in an int for gain < 65536. */
	if (volume < 0)
		volume = 0;
	if (volume > 1)
		volume = 1;
	gain = (int)(volume * 65536.0f + 0.5f);
	if (gain >= 65536)
		return done;
	if (gain <= 0)
	{
		memset (out, stream->bits == 8 ? 128 : 0, done);
		return done;
	}
	if (stream->bits == 8)
	{
		for (i = 0; i < done; i++)
			out[i] = (unsigned char)((((int)out[i] - 128) * gain) / 65536 + 128);
	}
	else
	{
		short *samples = (short *)out;
		for (i = 0; i < done / 2; i++)
			samples[i] = (short)(((int)samples[i] * gain) / 65536);
	}
	return done;
}
