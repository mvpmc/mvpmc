/*
 *  Copyright (C) 2004, Jon Gettler
 *  http://mvpmc.sourceforge.net/
 *
 *  wav file player by Stephen Rice
 *  ac3 code from a52dec
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <mvp_widget.h>
#include <mvp_av.h>

#include "mvpmc.h"

#include "a52dec/a52.h"
#include "a52dec/mm_accel.h"

static int fd = -1;

#define BSIZE	(1024*32)

a52_state_t *a52_state;
static int disable_adjust=0;
static int disable_dynrng=0;
static float gain = 1;
static char *ac3_buf, *ac3_end;
static int ac3_size;

typedef enum {
	AUDIO_FILE_UNKNOWN,
	AUDIO_FILE_MP3,
	AUDIO_FILE_AC3,
	AUDIO_FILE_WAV,
} audio_file_t;

static audio_file_t audio_type;

#define BUFFSIZE 128

void audio_play(mvp_widget_t *widget);

static int quantised_next_input_sample = 0;
static int last_sample_in_buffer = 0;
static int next_output_sample = 0;
static float next_input_sample = 0;
static float wav_file_to_input_frequency_ratio = 1;
static int chunk_size = 0;

#define min(X, Y)  ((X) < (Y) ? (X) : (Y))

static inline uint16_t convert (int32_t j)
{ int sample;

  j >>= 15;
  sample=((j > 32767) ? 32767 : ((j < -32768) ? -32768 : j));

  if(sample >= (1 << 15)) sample -= (1 << 16);
  sample += (1<<15);
  return(sample);
}

void ao_convert(sample_t* samples, uint16_t *out, int flags)
{
    int i;
    
    for (i=0;i<256;i++) {
        out[2*i] = convert (samples[i]);
        out[2*i+1] = convert (samples[i+256]);
    }
}

static int
find_chunk(int fd,char searchid[5])
{
	int found = 0;
	unsigned char buf[8];
	int size;

	while (found == 0) {
		size = read(fd, buf, 8);
		if (size <=0)
			return -1;
		size = buf[4] + (buf[5] << 8) + (buf[6] << 16) +
			(buf[7] << 24);
		buf[4] = 0;
		if (buf[0] != searchid[0] || buf[1] != searchid[1] ||
		    buf[2] != searchid[2] || buf[3] != searchid[3])
			lseek(fd,size,SEEK_CUR);
		else
			found = 1;
	}

	return size;
}

static void
pcm_play(int fd, int afd, unsigned short align, unsigned short channels,
	 unsigned short bps)
{
	unsigned char *wav_buffer;
	unsigned char pcm_buffer[BUFFSIZE][4];
	int n;
	int input_ptr;
	int iloop;
	int sample_value = 0;

	wav_buffer = alloca(align * BUFFSIZE * 2);
	n = read(fd, wav_buffer, min((align * BUFFSIZE), chunk_size));

	/*
	 * while we have data in the buffer
	 */
	last_sample_in_buffer += (n / align);
	while (quantised_next_input_sample < last_sample_in_buffer) {
		input_ptr = (quantised_next_input_sample % BUFFSIZE) * align;;

		for(iloop = 0;iloop<channels;iloop++) {
			if(bps == 1) {
				sample_value = wav_buffer[input_ptr + iloop] << 8;
			} else {
				sample_value = (wav_buffer[input_ptr + (iloop * bps) + bps - 1 ] <<8 ) + wav_buffer[input_ptr + (iloop * bps) + bps - 2 ];
				//convert signed to unsigned value
				if (sample_value >= (1 << 15))
					sample_value -= (1 << 16);
				sample_value += (1<<15);
			}
			pcm_buffer[next_output_sample][0+(iloop<<1)] =
				(sample_value >> 8);
			pcm_buffer[next_output_sample][1+(iloop<<1)] =
				(sample_value );
		}

		/*
		 * if mono wav file set the right channel to be the same as
		 * the left
		 */
		if(channels == 1) {
			pcm_buffer[next_output_sample][2] =
				pcm_buffer[next_output_sample][0];
			pcm_buffer[next_output_sample][3] =
				pcm_buffer[next_output_sample][1];
		}

		next_output_sample ++;
		if(next_output_sample == BUFFSIZE) {
			write(afd, pcm_buffer, (next_output_sample *4));
			next_output_sample = 0;
		}

		next_input_sample += wav_file_to_input_frequency_ratio;
		quantised_next_input_sample = next_input_sample;
	}
}

static int
audio_player(int reset)
{
	static char buf[BSIZE];
	static int n = 0, nput = 0, afd = 0, align;
	static unsigned short channels;
	static unsigned short bps;
	static int pcm_decoded = 0;
	static int ac3more = 0;
	int tot, len;
	static int ac3len = 0;

	if (reset) {
		n = 0;
		nput = 0;
		if (audio_type == AUDIO_FILE_WAV) {
			int rate;

			read(fd, buf, 12);
			if (buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' ||
			    buf[3] != 'F' || buf[8] != 'W' || buf[9] != 'A' ||
			    buf[10] != 'V' || buf[11] != 'E') {
				return -1;
			}

			chunk_size = find_chunk(fd, "fmt ");
			read(fd, buf, chunk_size);
			rate = buf[4] + (buf[5] << 8) + (buf[6] << 16) +
				(buf[7] << 24);
			align = buf[12] + (buf[13] << 8);
			channels = buf[2] + (buf[3] << 8);
			bps = align / channels;
			printf("WAVE file rate %d align %d channels %d\n",
			       rate, align, channels);
			av_set_pcm_rate(rate);

			chunk_size = find_chunk(fd, "data");
			printf("WAVE chunk size %d\n", chunk_size);

			quantised_next_input_sample = 0;
			last_sample_in_buffer = 0;
			next_output_sample = 0;
			next_input_sample = 0;
		}
		if (audio_type == AUDIO_FILE_AC3) {
			pcm_decoded = 0;
			ac3more = 0;
			ac3len = 0;
			printf("playing AC3 file\n");
		}
		afd = av_audio_fd();
	}

	switch (audio_type) {
	case AUDIO_FILE_AC3:
		if (ac3more == -1)
			ac3more = a52_decode_data(NULL, NULL, pcm_decoded);
		if (ac3more == 1)
			ac3more = a52_decode_data(buf, buf + ac3len,
						  pcm_decoded);
		if (ac3more == 0) {
			ac3len = read(fd, buf, BSIZE);
			ac3more = a52_decode_data(buf, buf + ac3len,
						  pcm_decoded);
		}
		pcm_decoded = 1;

		if (ac3more != 0) {
			mvpw_set_idle(NULL);
			mvpw_set_timer(root, audio_play, 100);
		}
		break;
	case AUDIO_FILE_WAV:
		pcm_play(fd, afd, align, channels, bps);
		break;
	case AUDIO_FILE_MP3:
		len = read(fd, buf+n, BSIZE-n);
		n += len;
		if ((tot=write(afd, buf+nput, n-nput)) == 0) {
			mvpw_set_idle(NULL);
			mvpw_set_timer(root, audio_play, 100);
			break;
		}
		nput += tot;

		if (nput == n) {
			n = 0;
			nput = 0;
		}
		break;
	case AUDIO_FILE_UNKNOWN:
		mvpw_set_idle(NULL);
		break;
	}

	return 0;
}

static int
get_audio_type(char *path)
{
	char *suffix;

	suffix = ".mp3";
	if ((strlen(path) >= strlen(suffix)) &&
	    (strcmp(path+strlen(path)-strlen(suffix), suffix) == 0))
		return AUDIO_FILE_MP3;

	suffix = ".ac3";
	if ((strlen(path) >= strlen(suffix)) &&
	    (strcmp(path+strlen(path)-strlen(suffix), suffix) == 0))
		return AUDIO_FILE_AC3;

	suffix = ".wav";
	if ((strlen(path) >= strlen(suffix)) &&
	    (strcmp(path+strlen(path)-strlen(suffix), suffix) == 0))
		return AUDIO_FILE_WAV;

	return AUDIO_FILE_UNKNOWN;
}

static void
audio_idle(void)
{
	int reset = 0;

	if (fd == -1) {
		switch ((audio_type=get_audio_type(current))) {
		case AUDIO_FILE_MP3:
			av_set_audio_output(AV_AUDIO_MPEG);
			av_set_audio_type(0);
			break;
		case AUDIO_FILE_AC3:
			av_set_audio_output(AV_AUDIO_PCM);
			break;
		case AUDIO_FILE_WAV:
			av_set_audio_output(AV_AUDIO_PCM);
			break;
		case AUDIO_FILE_UNKNOWN:
			mvpw_set_idle(NULL);
			return;
			break;
		}

		if ((fd=open(current, O_RDONLY|O_LARGEFILE|O_NDELAY)) < 0)
			return;
		av_play();

		reset = 1;
	}

	if (audio_player(reset) < 0)
		mvpw_set_idle(NULL);
}

void
audio_play(mvp_widget_t *widget)
{
	mvpw_set_idle(audio_idle);
	mvpw_set_timer(root, NULL, 0);
}

void
audio_clear(void)
{
	fd = -1;
	av_reset();
}

static void
buffer_ac3(uint16_t *buf, int size)
{
	memcpy(ac3_end, buf, size);
	ac3_end += size;
}

static int
flush_ac3(void)
{
	int n, len;
	static int total = 0;

	if (ac3_end == ac3_buf)
		return 0;

	len = ac3_end - ac3_buf;
	n = write(fd_audio, ac3_buf, len);
	if (n > 0)
		total += n;

	if (len == n) {
#if 0
		printf("AC3: flushed %d bytes (total %d)\n", len, total);
#endif
		ac3_end = ac3_buf;
		return 0;
	} else {
#if 0
		printf("AC3: wrote %d of %d bytes\n", n, len);
#endif
	}

	if (n > 0) {
		memmove(ac3_buf, ac3_buf+n, len-n);
		ac3_end -= n;
	}

	return -1;
}

int a52_decode_data (uint8_t * start, uint8_t * end, int reset)
{
	static uint8_t buf[3840];
	static uint8_t * bufptr = buf;
	static uint8_t * bufpos = buf + 7;

	/*
	 * sample_rate and flags are static because this routine could
	 * exit between the a52_syncinfo() and the ao_setup(), and we want
	 * to have the same values when we get back !
	 */

	static int sample_rate;
	static int flags;
	static int bit_rate;
	static int len;

	if (ac3_buf == NULL) {
                ac3_size = 1024*256;
                ac3_buf = malloc(ac3_size);
                ac3_end = ac3_buf;
	}

	flush_ac3();

	if (start == NULL)
		return 0;

	if ((ac3_end - ac3_buf) > (1024*128)) {
#if 0
		printf("AC3: buffer full (%d bytes)\n", (ac3_end - ac3_buf));
#endif
		return 1;
	}

	while (1) {
		len = end - start;
		if (!len)
			break;
		if (len > bufpos - bufptr)
			len = bufpos - bufptr;
		memcpy (bufptr, start, len);
		bufptr += len;
		start += len;
		if (bufptr == bufpos) {
			if (bufpos == buf + 7) {
				int length;

				length = a52_syncinfo (buf, &flags,
						       &sample_rate,
						       &bit_rate);
#if 0
				printf("a52 flags %d 0x%.8x\n", flags, flags);
#endif
				if (!length) {
#if 0
					fprintf (stderr, "skip\n");
#endif
					for (bufptr = buf; bufptr < buf + 6;
					     bufptr++)
						bufptr[0] = bufptr[1];
					continue;
				}
				bufpos = buf + length;
			} else {
				// The following two defaults are taken from audio_out_oss.c:
				level_t level=(1 << 26);
				sample_t bias=0;
				int i;
				uint16_t out[256*2];
				
				flags = A52_STEREO;
				if (!disable_adjust)
					flags |= A52_ADJUST_LEVEL;
				level = (level_t) (level * gain);
				if (a52_frame (a52_state, buf, &flags, &level,
					       bias))
					goto error;
				if (disable_dynrng)
					a52_dynrng (a52_state, NULL, NULL);
				if (!reset)
					av_set_pcm_rate(sample_rate);

				for (i = 0; i < 6; i++) {
					if (a52_block (a52_state))
						goto error;
					ao_convert (a52_samples (a52_state),
						    out, flags);
					buffer_ac3(out, sizeof(uint16_t)*256*2);
				}
				bufptr = buf;
				bufpos = buf + 7;
				continue;
			error:
				fprintf (stderr, "error\n");
				bufptr = buf;
				bufpos = buf + 7;
			}
		}
	}

	flush_ac3();

	/*
	 * XXX: prevent the buffer from overfilling
	 */
	if ((ac3_end - ac3_buf) > (1024*128)) {
#if 0
		printf("AC3: buffer full (%d bytes)\n", (ac3_end - ac3_buf));
#endif
		return -1;
	}

	return 0;
}
