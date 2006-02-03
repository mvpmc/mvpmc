/*
 *  Copyright (C) 2004,2005, Jon Gettler
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
#include <assert.h>
#include <sys/time.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>   
#include <sys/socket.h>   
#include <fcntl.h>

extern int errno;

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>

#include "mvpmc.h"
#include "mclient.h"

#define LIBA52_FIXED
#include "a52dec/a52.h"
#include "a52dec/mm_accel.h"

#include "tremor/ivorbiscodec.h"
#include "tremor/ivorbisfile.h"

static int http_play(int afd);
long bytesRead;
#define  STREAM_PACKET_SIZE  1448     

static char pcmout[4096];
static OggVorbis_File vf;
static int current_section;
static vorbis_info *vi;
static FILE *oggfile;

#define BSIZE		(1024*32)
#define AC3_SIZE	(1024*512)

a52_state_t *a52_state;
static int disable_adjust=0;
static int disable_dynrng=0;
static float gain = 1;
static unsigned char ac3_buf[AC3_SIZE];
static volatile unsigned char *ac3_end;
static int ac3_size = AC3_SIZE;
static volatile int ac3_head = 0, ac3_tail = AC3_SIZE - 1;

pthread_cond_t audio_cond = PTHREAD_COND_INITIALIZER;

typedef enum {
	AUDIO_FILE_UNKNOWN,
	AUDIO_FILE_MP3,
	AUDIO_FILE_AC3,
	AUDIO_FILE_WAV,
	AUDIO_FILE_OGG,
	AUDIO_FILE_HTTP_MP3,
	AUDIO_FILE_HTTP_INIT_OGG,
	AUDIO_FILE_HTTP_OGG,
	VIDEO_FILE_HTTP_MPG,
} audio_file_t;

int http_playing = 0;

static audio_file_t audio_type;

#define BUFFSIZE 12000

void audio_play(mvp_widget_t *widget);
static int wav_play(int, int, unsigned short, unsigned short, unsigned short);

static unsigned long quantised_next_input_sample = 0;
static unsigned long last_sample_in_buffer = 0;
static unsigned long next_output_sample = 0;
static unsigned long next_input_sample = 0;
static unsigned wav_file_to_input_frequency_ratio = 1;
static int chunk_size = 0;

static int align;
static unsigned short channels;
static unsigned short bps;
static int pcm_decoded = 0;

static int ac3_freespace(void);

#define min(X, Y)  ((X) < (Y) ? (X) : (Y))

av_passthru_t audio_output_mode = AUD_OUTPUT_STEREO;

volatile int audio_playing = 0;
volatile int audio_stop = 0;

#ifdef DEBUG_LOG_SOUND
static inline void logwrite (char *fn, int result, int fd, int size, unsigned char *data)
{
  int lfd;
  struct {
    int ssize;
    struct timeval tv;
    int result;
    int fd;
    int size;
  } lhdr;

  lfd = open(fn, O_CREAT|O_APPEND|O_WRONLY, 0666);
  if (lfd >= 0) {
    lhdr.ssize = sizeof(lhdr);
    gettimeofday(&lhdr.tv, NULL);
    lhdr.result = result;
    lhdr.fd = fd;
    lhdr.size = size;
    write(lfd, &lhdr, sizeof(lhdr));
    write(lfd, data, result);
    close(lfd);
  }
}
#endif

static inline uint16_t
convert(int32_t j)
{
	int sample;

	j >>= 15;
	sample = ((j > 32767) ? 32767 : ((j < -32768) ? -32768 : j));

	if (sample >= (1 << 15))
		sample -= (1 << 16);
	sample += (1<<15);

	return(sample);
}

static void
ao_convert(sample_t* samples, uint16_t *out, int flags)
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

static int
ogg_play(int afd)
{
	static int do_read = 1;
	static int ret = 0;
	static int pos = 0, len = 0;

	if (afd < 0) {
		pos = 0;
		len = 0;
		do_read = 1;
		return -1;
	}

	if (do_read) {
		if (http_playing==1) {
			do {
				ret = ov_read(&vf, pcmout,STREAM_PACKET_SIZE,
					      &current_section);
				bytesRead+=ret;
			} while (ret == OV_HOLE );
		} else {
			ret = ov_read(&vf, pcmout, sizeof(pcmout),
				      &current_section);
		}
		pos = 0;
		len = 0;
	}

	if (ret == 0) {
		fprintf(stderr, "EOF during ogg read...\n");
		goto next;
	} else if (ret < 0) {
		fprintf(stderr, "Error during ogg read...\n");
		goto next;
	} else {
		do {
			pos += len;
			if ((len = write(afd, pcmout + pos, ret - pos)) == -1) {
				fprintf(stderr,
					"Error during audio write\n");
				goto next;
			}
			if (len == 0)
				goto full;
		} while (pos + len < ret);
	}

	do_read = 1;

	/*
	 * We shouldn't have any problem filling up the hardware audio buffer,
	 * so keep processing the file until we're forced to block.
	 */
	return 0;

 full:
	do_read = 0;
	usleep(1000);
	return 0;

 next:
    http_playing = 0;
	return -1;
}

/******************************************
 * Function to play AC3 by mixing down from
 * 5.1 to 2 channels.
 ******************************************
 */
static int
ac3_play(int afd)
{
	static unsigned char buf[BSIZE];
	static int ac3more = 0;
	static int ac3len = 0;

	if (afd < 0) {
		pcm_decoded = 0;
		ac3more = 0;
		ac3len = 0;
		printf("playing AC3 file\n");
		return -1;
	}

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
		usleep(1000);
		return 0;
	}
	else if(ac3len==0){
		/* fprintf(stderr,"ac3 file finished\n"); */
		return -1;
	}

	return 0;
}

/******************************************
 * Function to play AC3 by passing data 
 * through spdif output.
 ******************************************
 */
static int
ac3_spdif_play(int afd)
{
        static char buf[BSIZE];
        static int n = 0, nput = 0;
        int tot, len;


        if (afd < 0) {
                n = 0;
                nput = 0;
                return -1;
        }

        len = read(fd, buf+n, BSIZE-n);
        n += len;
        if(n==0 && nput==0){
                /* fprintf(stderr,"ac3 file finished\n"); */
                return -1;
        }
        if ((tot=write(afd, buf+nput, n-nput)) == 0) {
		usleep(1000);
                return 0;
        }
        nput += tot;

        if (nput == n) {
                n = 0;
                nput = 0;
        }

	return 0;
}

static int
mp3_play(int afd)
{
	static char buf[BSIZE];
	static int n = 0, nput = 0;
	int tot, len;

	if (afd < 0) {
		n = 0;
		nput = 0;
		return -1;
	}

	if ((len=read(fd, buf+n, BSIZE-n)) == 0) {
		usleep(1000);
	}
	if (len < 0)
		return -1;
	n += len;
	if(n==0 && nput==0){
		/* fprintf(stderr,"mp3 file finished\n"); */
		return -1;
	}
	if ((tot=write(afd, buf+nput, n-nput)) == 0) {
		usleep(1000);
		return 0;
	}
	nput += tot;

	if (nput == n) {
		n = 0;
		nput = 0;
	}

	return 0;
}

static int
audio_player(int reset, int afd)
{
	int ret = -1;

	switch (audio_type) {
	case AUDIO_FILE_AC3:
		if (audio_output_mode == AUD_OUTPUT_STEREO) {
			/*
			 * downmixing from 5.1 to 2 channels
			 */
			ret = ac3_play(afd);
		} else {
			/*
			 * AC3 pass through out the SPDIF / TOSLINK
			 */
			ret = ac3_spdif_play(afd);
		}
		break;
	case AUDIO_FILE_WAV:
		ret = wav_play(fd, afd, align, channels, bps);
		break;
	case AUDIO_FILE_MP3:
		ret = mp3_play(afd);
		break;
	case AUDIO_FILE_HTTP_MP3:
		ret = http_play(afd);
		break;
	case AUDIO_FILE_OGG:
	case AUDIO_FILE_HTTP_OGG:
		ret = ogg_play(afd);
		break;
	case VIDEO_FILE_HTTP_MPG:
	default:
		break;
	}

	return ret;
}

static int
get_audio_type(char *path)
{
	char *suffix;

	if (strstr(path,"http://")!=NULL ) {
		if (audio_type != AUDIO_FILE_HTTP_OGG ) {
			return AUDIO_FILE_HTTP_MP3;
		} else {
			return AUDIO_FILE_HTTP_OGG;
		}
	}

	suffix = ".mp3";
	if ((strlen(path) >= strlen(suffix)) &&
	    (strcmp(path+strlen(path)-strlen(suffix), suffix) == 0))
		return AUDIO_FILE_MP3;

	suffix = ".ogg";
	if ((strlen(path) >= strlen(suffix)) &&
	    (strcmp(path+strlen(path)-strlen(suffix), suffix) == 0))
		return AUDIO_FILE_OGG;

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

static int
wav_setup(void)
{
	int rate, format, bwidth, bsample;
	unsigned char buf[BSIZE];

	if (read(fd, buf, 12) != 12)
		return -1;
	if (buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' ||
	    buf[3] != 'F' || buf[8] != 'W' || buf[9] != 'A' ||
	    buf[10] != 'V' || buf[11] != 'E') {
		return -1;
	}

	chunk_size = find_chunk(fd, "fmt ");
	if (read(fd, buf, chunk_size) != chunk_size)
		return -1;
	format = buf[0] + (buf[1] << 8);
	channels = buf[2] + (buf[3] << 8);
	rate = buf[4] + (buf[5] << 8) + (buf[6] << 16) + (buf[7] << 24);
	bwidth = buf[8] + (buf[9] << 8) + (buf[10] << 16) + (buf[11] << 24);
	align = buf[12] + (buf[13] << 8);
	bsample = buf[14] + (buf[15] << 8);
	bps = align / channels;

	if (format != 1) {
		fprintf(stderr, "Unrecognized WAV file!\n");
		return -1;
	}

	printf("WAVE file rate %d bandwidth %d align %d bits/sample %d channels %d\n",
	       rate, bwidth, align, bsample, channels);

	av_set_audio_output(AV_AUDIO_PCM);
	if (av_set_pcm_param(rate, 1, 2, 0, 16) < 0)
		return -1;

	chunk_size = find_chunk(fd, "data");
	printf("WAVE chunk size %d\n", chunk_size);

	return 0;
}

static int
ogg_setup(void)
{
	oggfile = fdopen(fd, "r");
	if (ov_open(oggfile, &vf, NULL, 0) < 0) {
		fprintf(stderr, "Failed to open Ogg file %s\n", current);
		return -1;
	}
	vi = ov_info(&vf, -1);

	printf("Bitstream is %d channel, %ldHz\n", vi->channels, vi->rate);

	av_set_audio_output(AV_AUDIO_PCM);
	if (av_set_pcm_param(vi->rate, 0, vi->channels, 0, 16) < 0)
		return -1;

	return 0;
}

void
audio_play(mvp_widget_t *widget)
{
	pthread_cond_signal(&audio_cond);
}

void
audio_clear(void)
{
	if(fd >= 0) {
		close(fd);
		fd = -1;
	}

	if (oggfile != NULL) {
		ov_clear(&vf);
		oggfile = NULL;
	}
}

void
empty_ac3(void)
{
	ac3_end = ac3_buf;
	ac3_head = 0;
	ac3_tail = ac3_size - 1;
}

static inline int
ac3_add(uint16_t *buf, int len)
{
	unsigned int end;
	int size1, size2;

	if (len <= 0)
		return 0;

	assert(len < ac3_size);
	assert(ac3_head != ac3_tail);

	end = (ac3_head + len + 6) % ac3_size;

	if (ac3_head > ac3_tail) {
		if ((end < ac3_head) && (end > ac3_tail)) {
			goto full;
		}
	} else {
		if ((end > ac3_tail) || (end < ac3_head)) {
			goto full;
		}
	}

	end = (ac3_head + len) % ac3_size;

	if (end > ac3_head) {
		size1 = len;
		size2 = 0;
	} else {
		size1 = ac3_size - ac3_head;
		size2 = end;
	}

	assert((size1 + size2) == len);

	memcpy(ac3_buf+ac3_head, buf, size1);
	if (size2) {
		memcpy(ac3_buf, ((unsigned char *)buf)+size1, size2);
	}

	ac3_head = end;

	assert(len == (size1 + size2));

	return (size1 + size2);

 full:
	printf("AC3 free bytes %d\n", ac3_freespace());
	printf("AC3 abort: len %d end %d head %d tail %d size %d\n",
	       len, end, ac3_head, ac3_tail, ac3_size);
	abort();

	return 0;
}

static int
ac3_freespace(void)
{
	int size1, size2;

	if (ac3_head >= ac3_tail) {
		size1 = ac3_head - ac3_tail - 1;
		size2 = 0;
	} else {
		size1 = ac3_size - ac3_tail - 1;
		size2 = ac3_head;
	}

	return (ac3_size - (size1 + size2));
}

static int
ac3_flush(void)
{
	int size1, size2;
	int n;

	if (ac3_head >= ac3_tail) {
		size1 = ac3_head - ac3_tail - 1;
		size2 = 0;
	} else {
		size1 = ac3_size - ac3_tail - 1;
		size2 = ac3_head;
	}

	if ((size1 + size2) == 0)
		goto empty;

	if (size1 > 0) {
		n = write(fd_audio, ac3_buf+ac3_tail+1, size1);
#ifdef DEBUG_LOG_SOUND
		logwrite("/video/output.ac3", n, fd_audio, size1, ac3_buf+ac3_tail+1);
#endif
		if (n < 0)
			return 0;
		if (n != size1) {
			size1 = n;
			size2 = 0;
			goto out;
		}
	}
	if (size2 > 0) {
		n = write(fd_audio, ac3_buf, size2);
#ifdef DEBUG_LOG_SOUND
		logwrite("/video/output.ac3", n, fd_audio, size2, ac3_buf);
#endif
		if (n < 0)
			size2 = 0;
		else
			size2 = n;
	}

 out:
	ac3_tail = (ac3_tail + size1 + size2) % ac3_size;

	return size1 + size2;

 empty:
	return 0;
}

static int
wav_play(int fd, int afd, unsigned short align, unsigned short channels,
	 unsigned short bps)
{
	unsigned char *wav_buffer;
	unsigned char pcm_buffer[BUFFSIZE][4];
	int n;
	int input_ptr;
	int iloop;
	int sample_value = 0;
	int empty = 0;

	if (ac3_flush() == 0)
		empty = 1;

	if (ac3_freespace() < (BUFFSIZE*align)) {
		usleep(1000);
		return 0;
	}

	wav_buffer = alloca(align * BUFFSIZE);
	n = read(fd, wav_buffer, min((align * BUFFSIZE), chunk_size));

	if ((n == 0) && empty)
		return 1;

	/*
	 * while we have data in the buffer
	 */
	last_sample_in_buffer += (n / align);
	while (quantised_next_input_sample < last_sample_in_buffer) {
		input_ptr = (quantised_next_input_sample % BUFFSIZE) * align;
		for (iloop = 0;iloop<channels;iloop++) {
			if (bps == 1) {
				sample_value = wav_buffer[input_ptr + iloop] << 8;
			} else {
				sample_value = (wav_buffer[input_ptr + (iloop * bps) + bps - 1 ] <<8 ) + wav_buffer[input_ptr + (iloop * bps) + bps - 2 ];
				//convert signed to unsigned value
				if (sample_value >= (1 << 15))
					sample_value -= (1 << 16);
				sample_value += (1<<15);
			}
			pcm_buffer[next_output_sample][0+(iloop<<1)] =
				(sample_value >> 8) & 0xFF;
			pcm_buffer[next_output_sample][1+(iloop<<1)] =
				sample_value & 0xFF;
		}

		/*
		 * if mono wav file set the right channel to be the same as
		 * the left
		 */
		if (channels == 1) {
			pcm_buffer[next_output_sample][2] =
				pcm_buffer[next_output_sample][0];
			pcm_buffer[next_output_sample][3] =
				pcm_buffer[next_output_sample][1];
		}

		next_output_sample ++;
		if (next_output_sample == BUFFSIZE) {
			ac3_add((uint16_t *)pcm_buffer, BUFFSIZE*align);
			next_output_sample = 0;
		}

		next_input_sample += wav_file_to_input_frequency_ratio;
		quantised_next_input_sample = next_input_sample;
	}

	if (next_output_sample != 0) {
		ac3_add((uint16_t *)pcm_buffer, next_output_sample * align);
		next_output_sample = 0;
	}

	ac3_flush();

	return 0;
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
	int n, total;

	ac3_flush();

	if (start == NULL)
		return 0;

	/*
	 * Make sure there is enough room in the buffer for whatever we
	 * want to put into it.
	 *
	 * XXX: This code needs to be rewritten so we can add partial
	 *      buffers and resume later when there is more room.
	 */
	total = end - start;
	if ((n=ac3_freespace()) < (total*14)) {
		return 1;
	}

	n = 0;
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
					av_set_pcm_param(sample_rate, 1, 2, 0, 16);

				for (i = 0; i < 6; i++) {
					if (a52_block (a52_state))
						goto error;
					ao_convert (a52_samples (a52_state),
						    out, flags);
					n += ac3_add(out, sizeof(uint16_t)*256*2);
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

	ac3_flush();

	return 0;
}

static void
sighandler(int sig)
{
}

static int
audio_init(void)
{   
	if ( strstr(current,"http://") == NULL   ) {
		if ((fd=open(current, O_RDONLY|O_LARGEFILE|O_NDELAY)) < 0) {
			goto fail;
		}
	}

	switch ((audio_type=get_audio_type(current))) {
	case AUDIO_FILE_HTTP_MP3:
	case AUDIO_FILE_MP3:
		av_set_audio_output(AV_AUDIO_MPEG);
		av_set_audio_type(0);
		break;        
	case AUDIO_FILE_OGG:
	case AUDIO_FILE_HTTP_OGG:
		if (ogg_setup() < 0)
			goto fail;
		break;
	case AUDIO_FILE_AC3:
		if (audio_output_mode == AUD_OUTPUT_STEREO) {
			/*
			 * downmixing from 5.1 to 2 channels
			 */
			av_set_audio_output(AV_AUDIO_PCM);
		} else {
			/*
			 * AC3 pass through out the SPDIF / TOSLINK
			 */
			if (av_set_audio_output(AV_AUDIO_AC3) < 0) {
				/* revert to downmixing */
				av_set_audio_output(AV_AUDIO_PCM);
				audio_output_mode = AUD_OUTPUT_STEREO;
			}
		}
		break;
	case AUDIO_FILE_WAV:
		if (wav_setup() < 0)
			goto fail;
		break;
	case AUDIO_FILE_UNKNOWN:
	case AUDIO_FILE_HTTP_INIT_OGG:
	case VIDEO_FILE_HTTP_MPG:
		goto fail;
		break;
	}

	av_play();

	switch (audio_type) {
	case AUDIO_FILE_WAV:
		empty_ac3();
		quantised_next_input_sample = 0;
		last_sample_in_buffer = 0;
		next_output_sample = 0;
		next_input_sample = 0;
		break;
	case AUDIO_FILE_AC3:
		if (audio_output_mode == AUD_OUTPUT_STEREO) {
			/*
			 * downmixing from 5.1 to 2 channels
			 */
			ac3_play(-1);
		} else {
			/*
			 * AC3 pass through out the SPDIF / TOSLINK
			 */
			ac3_spdif_play(-1);
		}
		break;
	case AUDIO_FILE_HTTP_OGG:
	case AUDIO_FILE_OGG:
		ogg_play(-1);
		break;
	case AUDIO_FILE_MP3:
		mp3_play(-1);
		break;
	case AUDIO_FILE_HTTP_MP3:
		http_play(-1);
		break;
	case AUDIO_FILE_HTTP_INIT_OGG:
	case VIDEO_FILE_HTTP_MPG:
	default:
		break;
	}

	return 0;

 fail:
	if (!playlist) {
		gui_error("Unable to play audio file!\n");
	}

	return -1;
}

/*
 * audio_start() - audio playback thread
 */
void*
audio_start(void *arg)
{
	sigset_t sigs;
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	int afd;
	int done;

	signal(SIGURG, sighandler);
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGURG);
	pthread_sigmask(SIG_UNBLOCK, &sigs, NULL);

	printf("audio thread started (pid %d)\n", getpid());

	pthread_mutex_lock(&mutex);

	while (1) {
		audio_playing = 0;
		audio_stop = 0;

		pthread_cond_wait(&audio_cond, &mutex);

	repeat:
		printf("Starting to play audio file '%s'\n", current);

		if (audio_init() != 0)
			goto fail;

		afd = av_audio_fd();

		audio_playing = 1;
		audio_stop = 0;

		while (((done=audio_player(0, afd)) == 0) &&
		       current && !audio_stop)
			; 

		if ( audio_type == AUDIO_FILE_HTTP_INIT_OGG ) {
			audio_type = AUDIO_FILE_HTTP_OGG;
			http_playing = 1;
			goto repeat;
		} else if ( audio_type == VIDEO_FILE_HTTP_MPG ) {
			http_playing = 1;
			av_reset();

			mvpw_set_timer(playlist_widget, NULL, 0);
			mvpw_hide(playlist_widget);
			mvpw_hide(fb_progress);
			video_set_root();
			mvpw_focus(root);
			screensaver_disable();
			mvpw_set_timer(root, video_play, 50);
			continue;
		} else  {
			audio_type = AUDIO_FILE_UNKNOWN;
		}

	fail:
		if (!audio_stop && playlist) {
			if (strncasecmp(current,"http://",7) ) {
				close(fd);
			}
			playlist_next();
			if (playlist) {
				printf("next song on playlist\n");
				goto repeat;
			}
		}

		printf("Done with audio file\n");

		close(fd);
		audio_clear();
	}

	return NULL;
}

typedef enum {
	CONTENT_MP3,
	CONTENT_PLAYLIST,
	CONTENT_OGG,
	CONTENT_MPG,
	CONTENT_PODCAST,
	CONTENT_UNKNOWN,
	CONTENT_REDIRECT,
} content_type_t;

typedef enum {
	PLAYLIST_SHOUT,
	PLAYLIST_M3U,
	PLAYLIST_PODCAST,
	PLAYLIST_NONE,
} playlist_type_t;

typedef enum {
	HTTP_INIT,
	HTTP_RESPONSE,
	HTTP_DATA,
	HTTP_PARSE,
	HTTP_RETRY,
	HTTP_UNKNOWN,
} http_state_type_t;

//=================================
// thse should be in mclient.h
typedef struct {
	void * buf;
	int head;
	int tail;
	int size;
	int playmode;
} ring_buf;

ring_buf* ring_buf_create(int size);

void send_mpeg_data(void);

#define RECV_BUF_SIZE 65536
#define OUT_BUF_SIZE  131072

//=================================

// globals from mclient.c

extern int debug;
extern ring_buf * outbuf;
extern void * recvbuf;
extern int local_paused;

int http_read_stream(unsigned int socket,int metaInt,int offset);

int http_main(void);
void http_osd_update(mvp_widget_t *widget);
void http_buffer(int message_length,int offset);
int http_metadata(char *metaString,int metaWork,int metaData);


#define  LINE_SIZE 256 
#define  MAX_URL_LEN 275
#define  MAX_PLAYLIST 5
#define  MAX_META_LEN 256

// note using Winamp now for testing

#define  GET_STRING "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: WinampMPEG/5.1\r\nAccept: */*\r\nIcy-MetaData:1\r\nConnection: close\r\n\r\n"
#define  GET_LIVE365 "GET /cgi-bin/api_login.cgi?action=login&org=live365&remember=Y&member_name=%s HTTP/1.0\r\nUser-Agent: Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8) Gecko/20051111 Firefox/1.5 \r\nHost: www.live365.com\r\nAccept: */*\r\nConnection: Keep-Alive\r\n\r\n"


int bufferFull;
char bitRate[10];
long contentLength;

void http_osd_update(mvp_widget_t *widget)
{
	av_stc_t stc;
	char buf[256];

	int percent;

	av_current_stc(&stc);
	snprintf(buf, sizeof(buf), "%.2d:%.2d:%.2d    %s",
		 stc.hour, stc.minute, stc.second,bitRate);
	mvpw_set_text_str(fb_time, buf);

    if (contentLength == 0) {
        snprintf(buf, sizeof(buf), "Bytes: %ld", bytesRead);
        mvpw_set_text_str(fb_size, buf);
        percent = (bufferFull* 100) /  OUT_BUF_SIZE;
    	snprintf(buf, sizeof(buf), "Buf:");
    } else {
        snprintf(buf, sizeof(buf), "Bytes: %ld", contentLength);
        mvpw_set_text_str(fb_size, buf);
        percent = (bytesRead * 100) /  contentLength;
    	snprintf(buf, sizeof(buf), "%d%%", percent);
    }
    mvpw_set_text_str(fb_offset_widget, buf);
    mvpw_set_graph_current(fb_offset_bar, percent);
    mvpw_expose(fb_offset_bar);

}

static int http_play(int afd)
{
    int rc = 0;
    char *ptr,*ptr1;
    if (afd==-1) {
        if (current) {           
            ptr = strstr(current,"http://");
            if (ptr!=NULL) {
                if (strlen(ptr) < MAX_URL_LEN ) {
                    rc = 0;
                    if (ptr!=current) {
                        // this logic is only for unpatched playlist.c remove directory
                        ptr1 = strdup(ptr);
                        free(current);
                        current = ptr1;
                    }
                    mvpw_set_timer(playlist_widget, http_osd_update, 500);
                } else {
                    rc = -1;
                }
            }
        } else {
            rc = -1;
        }
    } else {
        if (current && strncmp(current,"http://",7)==0 ) {
            ptr = current;
            while (*ptr!=0) {
                if (*ptr=='\n' || *ptr=='\r') {
                    *ptr=0;
                }
                ptr++;
            }
            mvpw_set_text_str(fb_name, current);
            outbuf = ring_buf_create(OUT_BUF_SIZE);
            recvbuf = (void*)calloc(1, RECV_BUF_SIZE);
            
//            local_paused = av_pause();
    
            http_main();
            free(outbuf->buf);
            free(outbuf);
            free(recvbuf);
            mvpw_set_timer(playlist_widget, NULL, 0);
            rc = 1;
        } else {
            rc = -1;
        }
    }
    return rc;
}

char shoutcastDisplay[41];

int http_main(void)
{

    struct sockaddr_in server_addr;          
    char get_buf[MAX_URL_LEN];    
//    unsigned char buffer[STREAM_PACKET_SIZE];     
    int  retcode;
    content_type_t ContentType;
    int httpsock=-1;
    int flags = 0;
  
    char url[MAX_PLAYLIST][MAX_URL_LEN];

    char scname[MAX_URL_LEN],scport[MAX_URL_LEN],scpage[MAX_URL_LEN];
    char host_name[MAX_URL_LEN];

    int counter=0;
    int  NumberOfEntries=1;
    int  curNumberOfEntries=0;
    int live365Login = 0;
    static char cookie[MAX_URL_LEN]={""};
    char user[MAX_URL_LEN],password[MAX_URL_LEN];

    char line_data[LINE_SIZE];
    char *ptr,*ptr1;
    int i;

    int metaInt;
    bytesRead = 0;
    
    int statusGet;

    struct hostent* remoteHost;
    int result;
    playlist_type_t playlistType;
    static char * ContentPlaylist[] = {"audio/x-scpls","audio/mpegurl","audio/mpeg-url","audio/x-mpegurl",
                                       "audio/x-mpeg-url","audio/m3u","audio/x-m3u", NULL};
    static char * ContentAudio[] = {"audio/mpeg","audio/x-mpeg","audio/mpg",NULL};


    retcode = 1;
    
    
    if ( MAX_URL_LEN < strlen(current) ) {
        sprintf(line_data,"URL length exceeds 256 characters");
        mvpw_set_text_str(fb_name, line_data);
        retcode = -1;
        return -1;
    }

    http_state_type_t stateGet = HTTP_UNKNOWN;

    snprintf(url[0],MAX_URL_LEN,"%s",current);
    curNumberOfEntries = 1;

    while (retcode == 1 && ++counter < 5 ) {
        
        if ( curNumberOfEntries <= 0 ) {
            mvpw_set_text_str(fb_name, "Invalid playlist");
            // no valid http in playlist
            retcode = -1;
            continue;
        }
        
        printf("%s\n",url[curNumberOfEntries-1]);

        live365Login = 0;

        if ( strstr(url[curNumberOfEntries-1],".live365.")!=NULL &&  strstr(url[curNumberOfEntries-1],"sessionid=")==NULL) {
            if (cookie[0] == 0 ) {
                if (getenv("LIVE365DATA")!=NULL) {                    
                    live365Login = 1;
                } 
            } else {
                ptr = strdup(url[curNumberOfEntries-1]);
                snprintf(url[curNumberOfEntries-1],MAX_URL_LEN,"%s?%s",ptr,cookie);
                free(ptr);
                printf("connect to %s\n",url[curNumberOfEntries-1]);
            }
        }

        retcode = 0;

        if (live365Login == 0 ) { 
            scpage[0]=0;

            // no size checks use same len for all
            result = sscanf(url[curNumberOfEntries-1],"http://%[^:/]:%[^//]%s",scname,scport,scpage);
            if (result==1) {
                result = sscanf(url[curNumberOfEntries-1],"http://%[^/]%s",scname,scpage);
                strcpy(scport,"80");
            }
            if (scpage[0]==0) {
                strcpy(scpage,"/");
            }
        } else {
            strcpy(scname,"www.live365.com");
            strcpy(scport,"80");
        }


        mvpw_set_text_str(fb_name, scname);
        strcpy(host_name,scname);
        remoteHost = gethostbyname(host_name);
//        printf("%s\n",remoteHost->h_name);
        if (remoteHost!=NULL) {
            
            if (live365Login == 0 ) { 
                snprintf(get_buf,MAX_URL_LEN,GET_STRING,scpage,remoteHost->h_name);
            } else {
                if (snprintf(get_buf,MAX_URL_LEN,GET_LIVE365,getenv("LIVE365DATA")) >= MAX_URL_LEN-1) {
                    retcode = -1;
                    continue;
                }
            }
//            printf("%s %s\n",scname,get_buf);


            httpsock = socket(AF_INET, SOCK_STREAM, 0);
    
            server_addr.sin_family = AF_INET;    
            server_addr.sin_port = htons(atoi(scport));           
            memcpy ((char *) &server_addr.sin_addr, (char *) remoteHost->h_addr, remoteHost->h_length);
            
            struct timeval stream_tv;

            stream_tv.tv_sec = 10;
            int optionsize = sizeof(stream_tv);
            setsockopt(httpsock, SOL_SOCKET, SO_SNDTIMEO, &stream_tv, optionsize);
//                                printf("Option = %d\n",option);


            mvpw_set_text_str(fb_name, "Connecting to server");

            retcode = connect(httpsock, (struct sockaddr *)&server_addr,sizeof(server_addr));
            if (retcode != 0) {
                printf("connect() failed \n");
                mvpw_set_text_str(fb_name, "Connection Error");
                retcode = -2;
                break;
            }
            

            
            // Send a GET to the Web server
            
            mvpw_set_text_str(fb_name, "Sending GET request");

            if (send(httpsock, get_buf, strlen(get_buf), 0) != strlen(get_buf) ){
                printf("send() failed \n");
                retcode = -2;
                break;
            }

    
            // Receive from the Web server
            //  - The return code from recv() is the number of bytes received
//            int whereami;
//            int line_start = 0 ;
//            int buflen = whereami = STREAM_PACKET_SIZE;
            
            FILE * instream;

            stateGet = HTTP_INIT;
            playlistType = PLAYLIST_NONE;
            contentLength = 0;
            metaInt = 0;
            bitRate[0]=0;


            instream = fdopen(httpsock,"rb");
            setbuf(instream,NULL);

            char *rcs;            
            rcs = fgets(line_data,LINE_SIZE-1,instream);
            ptr = strrchr (line_data,'\r');
            if (ptr!=NULL) {
                *ptr =0;
            }
            printf("%s\n",line_data);


            if (line_data[0]==0) {
                retcode = -2;
                break;
            } else {
                ptr = line_data;
                while (*ptr!=0 && *ptr!=0x20) {
                    ptr++;
                }
                if (*ptr==0x20) {
                    sscanf(ptr,"%d",&statusGet);
                }
                if (statusGet != 200 && statusGet != 302) {
                    if ( NumberOfEntries > 1 && NumberOfEntries < curNumberOfEntries ) {
                        // try next
                        curNumberOfEntries++;
                        close(httpsock);
                        continue;
                    } else {
                        mvpw_set_text_str(fb_name, line_data);
                        retcode = -2;
                        break;
                    }
                }
            }
            mvpw_set_text_str(fb_name, scname);


            if (strcmp(line_data,"ICY 200 OK")==0) {
                // default shoutcast to mp3 when none found
                ContentType = CONTENT_MP3;
            } else {
                ContentType = CONTENT_UNKNOWN;
            }

            stateGet = HTTP_RESPONSE;
            retcode = -2;
            while ( rcs != NULL && !ferror(instream) && !feof(instream ) ) {
                rcs = fgets(line_data,LINE_SIZE-1,instream);
                if (rcs==NULL) {
                    printf("fget() failed \n");
                    continue;
                }


                ptr = strrchr (line_data,'\r');
                if (ptr!=NULL) {
                    *ptr =0;
                }
                ptr = strrchr (line_data,'\n');
                if (ptr!=NULL) {
                    *ptr =0;
                }
              
                printf("%s\n",line_data);
                
                if ( line_data[0]==0x0a || line_data[0]==0x0d || line_data[0]==0) {
                    if (ContentType==CONTENT_MP3 || ContentType==CONTENT_OGG || ContentType==CONTENT_MPG || statusGet==302) {
                        // stream the following audio data or redirect
                        break;
                    } else {
                        if (stateGet!=HTTP_DATA) {
                            stateGet = HTTP_PARSE;                        
                        }
                        continue;
                    }

                }
                if (stateGet == HTTP_RESPONSE ) {
                    // parse response

                    if (strncasecmp(line_data,"Content-Length:",15)==0) {
                        sscanf(&line_data[15],"%ld",&contentLength);
                    } else if (strncasecmp(line_data,"Content-Type",12)==0) {

                        if ( strstr(line_data,"audio/") != NULL ) {                            
                            i = 0;
                            // have to do this one first some audio would match playlists
                            while (ContentPlaylist[i]!=NULL) {
                                ptr = strstr(line_data,ContentPlaylist[i]);
                                if (ptr!=NULL) {
                                    if (i!=0) {
                                        playlistType = PLAYLIST_M3U;
                                    } else {
                                        playlistType = PLAYLIST_SHOUT;
                                    }
                                    ContentType = CONTENT_PLAYLIST;
                                    curNumberOfEntries = -1;   // start again
                                    break;
                                } else {
                                    i++;
                                }
                            }
                            if (ContentType!=CONTENT_PLAYLIST) {
                                i = 0;
                                while (ContentAudio[i]!=NULL) {
                                    ptr = strstr(line_data,ContentAudio[i]);
                                    if (ptr!=NULL) {                    
                                        ContentType = CONTENT_MP3;
                                        break;
                                    } else {
                                        i++;
                                    }
                                }
                            }
                        } else if ( strstr(line_data,"application/ogg") != NULL ) {
                            ContentType = CONTENT_OGG;
                        } else if ( strstr(line_data,"video/mpeg") != NULL ) {
                            ContentType = CONTENT_MPG;
                        } else if ( playlistType == PLAYLIST_NONE && (strstr(line_data,"text/xml") != NULL || strstr(line_data,"application/xml") != NULL ) ) {
                            // try and find podcast data
                            ContentType = CONTENT_PODCAST;
                            playlistType = PLAYLIST_PODCAST ;
                            curNumberOfEntries = -1;   // start again
                        } else {
                            ContentType = CONTENT_UNKNOWN;
                        } 

                    } else if (strncasecmp (line_data, "icy-metaint:", 12)==0) {
                        sscanf(&line_data[12],"%d",&metaInt);
//                        printf("Metadata interval %d\n",metaInt);
                    } else if (live365Login==1 && strncasecmp (line_data, "Set-Cookie:",11)==0) {
                        ptr = strstr(line_data,"sessionid=");
                        if (ptr!=NULL) {
                            ContentType = CONTENT_REDIRECT;
                            sscanf(ptr,"%[^%]%[^;]",user,password);
                            snprintf(cookie,MAX_URL_LEN,"%s:%s",user,&password[3]);
                            printf("%s\n",cookie);
                            stateGet=HTTP_RETRY;
                            live365Login = 2;
                            break;

                        }
                    } else if (strncasecmp (line_data, "icy-name:",9)==0) {
                        line_data[70]=0;
                        mvpw_set_text_str(fb_name, &line_data[9]);
                        mvpw_menu_change_item(playlist_widget,playlist->key, &line_data[9]);
                        snprintf(shoutcastDisplay,40,&line_data[9]);
                    } else if (strncasecmp (line_data, "icy-br:",6)==0) {
                        snprintf(bitRate,10,"kbps%s",&line_data[6]);
                    } else if ( statusGet==302) {
                        if (strncasecmp (line_data,"Location:" ,9 )==0) {
                            ptr = strstr(line_data,"http://");
                            if (ptr!=NULL &&  (strlen(ptr) < MAX_URL_LEN) ) {
//                                free(current);
//                                current = strdup(ptr);
                                snprintf(url[0],MAX_URL_LEN,"%s",ptr);
                                stateGet=HTTP_RETRY;
                                ContentType = CONTENT_REDIRECT;
                            }
                        }
                    }

                } else {
                    // parse non-audio data
                    if (playlistType == PLAYLIST_NONE) {
                        ptr = strstr(line_data,"[playlist]");
                        if (ptr!=NULL) {
                            playlistType = PLAYLIST_SHOUT;
                            ContentType = CONTENT_PLAYLIST;
                            curNumberOfEntries = -1;   // start again
                            NumberOfEntries = 1;
                        }
                    } else if (strncmp(line_data,"File",4)==0 && line_data[5]=='=') {
                        if ( strncasecmp(&line_data[6],"http://",7)==0 && strlen(&line_data[6]) < MAX_URL_LEN ) {                        
                            if (curNumberOfEntries <= MAX_PLAYLIST ) {
                                if (curNumberOfEntries == -1 ) {
                                    // assume 1 before number of entries
                                    curNumberOfEntries = 0;
                                    NumberOfEntries = 1;
                                }
//                                free(current);
//                                current = strdup(&line_data[6]);
                                snprintf(url[curNumberOfEntries],MAX_URL_LEN,"%s",&line_data[6]);
                                curNumberOfEntries++;
                                stateGet = HTTP_RETRY;
                                if (curNumberOfEntries == NumberOfEntries ) {
                                    // no need for any more
                                    break;
                                }


                            }
                        }
                    } else if (strncasecmp(line_data,"NumberOfEntries=",16)==0) {
                        if (curNumberOfEntries == -1 ) {
                            // only read the first entry
                            NumberOfEntries= atoi(&line_data[16]);
                            curNumberOfEntries=0;
                        }
                    } else if (playlistType == PLAYLIST_M3U && strncasecmp(line_data,"http://",7) == 0 && strlen(line_data) < MAX_URL_LEN) {
                        if (curNumberOfEntries <= MAX_PLAYLIST ) {
                            if (curNumberOfEntries == -1 ) {
                                // assume 1 before number of entries
                                curNumberOfEntries = 0;
                                NumberOfEntries = 0;

                            }
//                            free(current);
//                            current = strdup(line_data);
                            snprintf(url[curNumberOfEntries],MAX_URL_LEN,"%s",line_data);
                            curNumberOfEntries++;
                            NumberOfEntries++;
                            stateGet = HTTP_RETRY;
                            if (curNumberOfEntries == MAX_PLAYLIST ) {
                                // no need for any more
                                break;
                            }
                        }
                    } else if (playlistType == PLAYLIST_PODCAST && (ptr=strstr(line_data,".mp3")) != NULL && strstr(line_data,"url") != NULL && (ptr1=strstr(line_data,"http://")) != NULL ) {
                        if (curNumberOfEntries <= MAX_PLAYLIST ) {
                            if (ptr1 < ptr && ((ptr - ptr1 + 4) < MAX_URL_LEN) ){
                                if (curNumberOfEntries == -1 ) {
                                    // assume 1 before number of entries
                                    curNumberOfEntries = 0;
                                    NumberOfEntries = 0;
    
                                }
                                *(ptr+4)=0;
//                                free(current);
//                                current = strdup(ptr1);
                                snprintf(url[curNumberOfEntries],MAX_URL_LEN,"%s",ptr1);
                                curNumberOfEntries++;
                                NumberOfEntries++;
                                stateGet = HTTP_RETRY;
                                break;
                            }

                        }
                    }
                }

            }
//            printf("%d %d %d %d %d %d\n",line_data[0],ContentType,stateGet,retcode,statusGet,curNumberOfEntries);
            if ( ferror(instream) ) {
                retcode = -2;
            } else {
                switch (ContentType) {
                    case CONTENT_MP3:
                        // good streaming data
                        // this data doesn't make it to mpg or ogg streams change to use fdopen etc.
    
                        // memcpy(recvbuf,&buffer[line_start],buflen - whereami);
                        // http_buffer(buflen - whereami,0);
                        
    
                        if (fcntl(httpsock, F_SETFL, flags | O_NONBLOCK) != 0) {
                            printf("nonblock fcntl failed \n");
                        } else {
                            http_read_stream(httpsock,metaInt,0);
                        }
                        retcode = -2;
                        break;
                    case CONTENT_OGG:
                        fd = httpsock;
                        audio_type = AUDIO_FILE_HTTP_INIT_OGG;
                        retcode = 3;
                        break;
                    case CONTENT_MPG:
                        if (fcntl(httpsock, F_SETFL, flags | O_NONBLOCK) != 0) {
                            printf("nonblock fcntl failed \n");
                        }

                        fd = httpsock;
                        int option = 65534; 
                        setsockopt(httpsock, SOL_SOCKET, SO_RCVBUF, &option, sizeof(option));
    //                    option = 0;
    //                    int optionsize = sizeof(int);
    //                    getsockopt(httpsock, SOL_SOCKET, SO_RCVBUF, &option, &optionsize);
    //                    printf("Option = %d\n",option);
                        audio_type = VIDEO_FILE_HTTP_MPG;
                        retcode = 3;
                        break;
                    case CONTENT_PLAYLIST:
                    case CONTENT_PODCAST:
                    case CONTENT_REDIRECT:
                        if (stateGet == HTTP_RETRY) {
                            // try again
                            curNumberOfEntries = 1;
                            printf("Retry %d %s\n",curNumberOfEntries,url[curNumberOfEntries-1]);
                            stateGet = HTTP_UNKNOWN;
                            close(httpsock);
                            retcode = 1;
                        } else {
                            retcode = -2;
                        }
                        break;
                    case CONTENT_UNKNOWN:
                    default:
                        mvpw_set_text_str(fb_name, "No valid content");
                        retcode = -2;
                        break;
    
                }
                
            }
        } else {
            printf("host_name %s not found %d\n",host_name,errno);
            mvpw_set_text_str(fb_name, "DNS Trouble Check /etc/resolv.conf");
            retcode = -1;
        }
    }
    if (contentLength==0 && audio_stop == 0 ) {
        audio_stop = 1;
    }

    if (retcode == -2 || retcode == 2) {
        close(httpsock);
    }
    if (gui_state == MVPMC_STATE_HTTP ) {
        mvpw_hide(mclient);
        gui_state = MVPMC_STATE_FILEBROWSER;
    }
    return retcode;
}


void http_buffer(int message_length, int offset)
{
    if (message_length == 0 ) {
//        printf("sendEmptyChunk\n");
    } else {
        bytesRead+=message_length;
        /*
         * Check if there is room at the end of the buffer for the new data.
         */
        if ((outbuf->head + message_length) <= OUT_BUF_SIZE ) {
            /*
             * Put data into the rec buf.
             */
            memcpy((outbuf->buf + outbuf->head),recvbuf+offset,message_length);

            /*
             * Move head by number of bytes read.
             */
            outbuf->head += message_length;
            if (outbuf->head == OUT_BUF_SIZE ) {
                outbuf->head = 0;
            }
        } else {
            /*
             * If not, then split data between the end and beginning of
             * the buffer.
             */
            memcpy((outbuf->buf + outbuf->head), recvbuf+offset, (OUT_BUF_SIZE - outbuf->head));
            memcpy(outbuf->buf,recvbuf + offset + (OUT_BUF_SIZE - outbuf->head), (message_length - (OUT_BUF_SIZE - outbuf->head)));

            /*
             * Move head by number of bytes written from the beginning of the buffer.
             */
            outbuf->head = (message_length - (OUT_BUF_SIZE - outbuf->head));
        }
    }
}
int http_metadata(char *metaString,int metaWork,int metaData)
{
    int retcode;
    char buffer[MAX_META_LEN];
    char *ptr;
    
    if (metaData > MAX_META_LEN-1 ) {
        metaData = MAX_META_LEN-1;
    }
    memcpy(buffer,recvbuf+metaWork,metaData);
    buffer[metaData]=0;
    
    if (strncmp(buffer,"StreamTitle=",12)==0) {
        char * fc;
        fc = strstr (buffer,"';StreamUrl='");
        if (fc!=NULL) {
            *fc = 0;
        } else  if ((fc = strstr(buffer,"';"))!=NULL) {
            *fc= 0;
        }        
        if (strcmp(metaString,&buffer[13])) {
            ptr = &buffer[13];
//            printf("md |%s|\n",ptr);
            while (*ptr!=0) ptr++;
            do {
                ptr--;            
            } while (*ptr==' ' && ptr != &buffer[13]);
            if (ptr != &buffer[13]) {
                snprintf(metaString,MAX_META_LEN,"%s",&buffer[13]);
                retcode = 0;
            } else {
                retcode = 3;
            }
        } else {
            retcode = 3;
        }
    } else if (strncmp(buffer,"StreamUrl=",10)==0) {
        retcode = 1;
    } else {
        printf("%x|%s|\n",buffer[0],buffer);
        retcode = 2;
    }
    return retcode;

}

int http_read_stream(unsigned int httpsock,int metaInt,int offset)
{
    int retcode=0;

    int metaRead = metaInt;
    int metaStart = 0;
    int metaWork=0;
    char cmetaData;
    int metaData=0;
    int metaIgnored=0;
    char peekBuffer[STREAM_PACKET_SIZE];

    char metaString[4081];
    char mclientDisplay[140];

    int message_len=offset;
    
    metaRead -= message_len;
    outbuf->playmode = 4;

    struct timeval stream_tv;
    fd_set read_fds;
    FD_ZERO(&read_fds);
    int dataFlag = 0;


//    debug = 1;        

    do {

        int n = 0;

        FD_ZERO(&read_fds);
        FD_SET(httpsock, &read_fds);
        if (httpsock > n)  n = httpsock ;

        /*
         * Wait until we receive data from server or up to 100ms
         * (1/10 of a second).
         */
        stream_tv.tv_usec = 100000;
        
        if (select(n + 1, &read_fds, NULL, NULL, &stream_tv) == -1) {
            if (errno != EINTR && errno != EAGAIN) {
                printf("select error\n");
                abort();
            }
        }
        if (outbuf->head >= outbuf->tail ) {
            bufferFull = outbuf->head - outbuf->tail;
        } else {
            bufferFull = outbuf->head + (OUT_BUF_SIZE - outbuf->tail);                    
        }
        if (outbuf->playmode == 4 && bufferFull > (OUT_BUF_SIZE / 2) ) {
            outbuf->playmode = 0;
            outbuf->tail = 0;
        } else if (outbuf->tail==0 && dataFlag > 20) {
            outbuf->playmode = 0;
        }

        /*
         * Check if the "select" event could have been caused because data
         * has been sent by the server.
         */

        if (FD_ISSET(httpsock, &read_fds) ) {
//            printf("%d %d %d %d %d %d\n",outbuf->playmode,dataFlag,bufferFull,outbuf->head,outbuf->tail,message_len);

            if (bufferFull < (OUT_BUF_SIZE - 2 * STREAM_PACKET_SIZE) ) {
                message_len = recv (httpsock, (char *)recvbuf, STREAM_PACKET_SIZE,0);
                if (message_len<=0) {
                    dataFlag++;
                } else {
                    dataFlag=0;
                }
                if (message_len < 0) {
                    if (errno!=EAGAIN) {
                        printf("recv() error %d\n",errno);
                    }
                } else if (metaInt==0) {
                    metaRead -= message_len;
                    http_buffer(message_len,0);
                } else {

                    if (metaRead < message_len) {
                        cmetaData =  ((char*)recvbuf)[metaRead];
                        metaData = (int) cmetaData;
                        metaWork = metaRead;
                        if ( metaData==0 ) {
                            http_buffer(metaRead,0);
                            metaStart = metaRead+1;
                            metaRead = metaInt;
                            if (metaStart < message_len) {
                                http_buffer(message_len-metaStart,metaStart);
                                metaRead = metaRead - (message_len-metaStart);
                            } else {
        //                                    printf("meta int 0\n");
                            }


                        } else {
                            http_buffer(metaRead,0);                                
                            metaData*=16;
//                          printf("meta %d\n",metaData);
                            metaStart = metaRead + metaData + 1;
                            if (metaStart <= message_len ) {
                                metaRead = metaInt;
                                if (http_metadata(metaString,metaWork+1,metaData)==0 ) {
                                    printf("%s\n",metaString);
                                    mvpw_set_text_str(fb_name, metaString);
                                    snprintf (mclientDisplay,80, "%-40.40s\n%-40.40s\n", shoutcastDisplay,metaString);
                                    if (gui_state != MVPMC_STATE_HTTP ) {
                                        switch_gui_state(MVPMC_STATE_HTTP);
//                                        mvpw_show(mclient);
                                        mvpw_focus(playlist_widget);
                                    }
                                    mvpw_set_dialog_text (mclient,mclientDisplay);
                                }
                                if ( metaStart < message_len ) {
                                    http_buffer(message_len-metaStart,metaStart);
                                    metaRead = metaRead - (message_len-metaStart);
                                } else {
                                    /* all read */
                                }
                            } else {
                                /* skip rest */
                                memcpy(metaString,recvbuf+metaRead+1,message_len-metaRead);
                                metaIgnored = message_len-metaRead;
                                metaStart -= message_len;
        //                                    printf("ignore %d %d\n",metaStart,metaIgnored);
                                metaRead = metaInt + metaStart;
                            }
                        }
                    } else {
                        // check this for optimizing
//                        printf ("%d %d\n",metaRead,message_len );

                        if (metaRead > metaInt) {
                            // meta data spans recvs - does not update screen

                            if ((metaStart+metaIgnored) > 4000) {
                                // probably because of metaint error probably insane
                                abort();
                            }

                            if ((metaRead-message_len) > metaInt ) {
                                /* all recv was meta data skip it all */
                                metaStart = metaRead - metaInt;                                
                                memcpy(metaString+metaIgnored,recvbuf,metaStart);
                                metaRead-=message_len;
                                metaIgnored += metaStart;
                                printf("ignore 2 %d\n",metaRead);
                            } else {
                                metaStart = metaRead - metaInt;
                                memcpy(metaString+metaIgnored,recvbuf,metaStart);
                                metaRead-=metaStart;
//                              printf("ignored %-64.64s|%d %d %d\n",metaString,metaRead,metaStart,metaIgnored);
                                http_buffer(message_len-metaStart,metaStart);
                                metaRead -= (message_len - metaStart);

                            }
                        } else {
                            metaRead -= message_len;
                            http_buffer(message_len,0);
                        }
                    }
                }
            } else {
                message_len = recv (httpsock, peekBuffer, STREAM_PACKET_SIZE,MSG_PEEK);
                usleep(10000);
            }
        }
        send_mpeg_data();
        usleep(1000);
    } while ( (message_len > 0 || bufferFull > 1) && audio_stop == 0 );
    if (audio_stop == 0) {
        // empty audio buffer
//        mvpw_set_text_str(fb_name, "Shutting Down");
        usleep(1000000);
    }
    bufferFull = 0;

//    printf("message %d buff %d stop %d\n",message_len,bufferFull,audio_stop);

    return retcode;
}



