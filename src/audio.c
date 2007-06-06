/*
 *  Copyright (C) 2004,2005,2006,2007, Jon Gettler
 *  http://www.mvpmc.org/
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
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>

extern int errno;
extern char *vlc_server;
extern char cwd[];

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>

#include "mvpmc.h"
#include "mclient.h"
#include "http_stream.h"

#define LIBA52_FIXED
#include "a52dec/a52.h"
#include "a52dec/mm_accel.h"

#include "tremor/ivorbiscodec.h"
#include "tremor/ivorbisfile.h"

int is_streaming(char *url);

typedef enum {
	OGG_STATE_UNKNOWN,
	OGG_STATE_BOS,
	OGG_STATE_OPEN,
	OGG_STATE_LASTPAGE,
	OGG_STATE_NEWPAGE,
	OGG_STATE_PLAYNEWPAGE,
	OGG_STATE_EOS,
} ogg_state_t;

int using_vlc = 0;
int using_helper = 0;
static int http_play(int afd);


static char pcmout[4096];
static OggVorbis_File vf;
static int current_section;
int OggStreamState = OGG_STATE_UNKNOWN;
static vorbis_info *vi;
struct my_oggHeader {
	char magic[4];
	char version;
	char type;
	long long granule_position;
	unsigned short bitstream_serial_number;
	unsigned short page_sequence_number;
	unsigned short CRC_checksum;
	char page_segments;
};
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
	AUDIO_FILE_MVP_MP3,
	AUDIO_FILE_HTTP_MP3,
	AUDIO_FILE_HTTP_OGG,
	AUDIO_FILE_FLAC,
	AUDIO_FILE_HTTP_FLAC,
} audio_file_t;


static audio_file_t audio_type;

#define BUFFSIZE 12000

void audio_play(mvp_widget_t *widget);
static int wav_play(int);

static int chunk_size = 0;

static int align;
static unsigned short channels;
static unsigned short bps;
static int pcm_decoded = 0;

static int ac3_freespace(void);
static int ac3_flush(void);

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


static int ogg_setup(void);

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
		if (http_playing==HTTP_AUDIO_FILE_OGG) {
			do {
				ret = ov_read(&vf, pcmout,sizeof(pcmout),
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

	if (OggStreamState==OGG_STATE_NEWPAGE) {
		OggStreamState=OGG_STATE_PLAYNEWPAGE;
		return 0;
	}

	if (ret == 0) {
		fprintf(stderr, "EOF during ogg read...\n");
		goto next;
	} else if (ret < 0 ) {
		fprintf(stderr, "Error during ogg read...%d\n",ret);
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
	http_playing = HTTP_FILE_CLOSED;
	OggStreamState = OGG_STATE_EOS;
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
	do {
		if ((len=read(fd, buf+n, BSIZE-n)) == 0) {
			usleep(1000);
		}
        } while (len < 0 && ( errno == EINTR || errno == EAGAIN )  );
	if (len < 0)
		return -1;
	n += len;
	if(n==0 && nput==0){
//		fprintf(stderr,"mp3 file finished\n");
		return -1;
	}

	if ((tot=write(afd, buf+nput, n-nput)) <= 0) {
		usleep(1000);
		return tot;
	}
	nput += tot;

	if (nput == n) {
		n = 0;
		nput = 0;
	}

	return 0;
}

static int flac_play(int afd);

static int
audio_player(int reset, int afd)
{
	int ret = -1;

	if (OggStreamState==OGG_STATE_PLAYNEWPAGE) {
		if ( ogg_setup() ) {
			return -1;
		}
	}
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
		ret = wav_play(afd);
		if (ret==1) {
			while (audio_stop == 0 && ac3_size > ac3_freespace()) {
				ac3_flush();
				usleep(10000);
			};
		}
		break;
	case AUDIO_FILE_MVP_MP3:
	case AUDIO_FILE_MP3:
		ret = mp3_play(afd);
		break;
	case AUDIO_FILE_HTTP_MP3:
		ret = http_play(afd);
		if (audio_type == AUDIO_FILE_HTTP_OGG || 
		    audio_type == AUDIO_FILE_HTTP_FLAC ){
			ret = 0;
		}
		break;
	case AUDIO_FILE_OGG:
	case AUDIO_FILE_HTTP_OGG:
		ret = ogg_play(afd);
		break;
	case AUDIO_FILE_FLAC:
	case AUDIO_FILE_HTTP_FLAC:
		ret = flac_play(afd);
		break;
	default:
		break;
	}

	return ret;
}

static int
get_audio_type(char *path)
{
	char *suffix;

	if (is_streaming(path) >=0 ) {
		if (audio_type != AUDIO_FILE_HTTP_OGG && 
		    audio_type != AUDIO_FILE_HTTP_FLAC) { 
			return AUDIO_FILE_HTTP_MP3;
		} else {
			return audio_type;
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
	
	suffix = ".flac";
	if ((strlen(path) >= strlen(suffix)) &&
	    (strcmp(path+strlen(path)-strlen(suffix), suffix) == 0))
		return AUDIO_FILE_FLAC;
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

size_t ogg_read_callback(void *ptr, size_t byteSize, size_t sizeToRead, void *datasource) 
{
	static char readBuffer[STREAM_PACKET_SIZE];
	static size_t bytesRemaining=0;

	size_t result;
	size_t toRead;
	size_t i;
	char *dataptr,*outptr;
	struct my_oggHeader *header;

	if (OggStreamState == OGG_STATE_UNKNOWN) {
		bytesRemaining = 0;
		OggStreamState = 0;
	} else if ( OggStreamState==OGG_STATE_NEWPAGE) {
		printf ("assume eos but shouldn't happen\n");
		return 0;
	}
	toRead = byteSize*sizeToRead;
	if ( toRead > STREAM_PACKET_SIZE ) {
		toRead = STREAM_PACKET_SIZE;
	}
	if (bytesRemaining < toRead) {
		// fill buffer from stream
		result = fread(&readBuffer[bytesRemaining],1,toRead-bytesRemaining,datasource);
		if (result <= 0 ) {
			printf ("ogg stream error\n");
			return result;
		}
		result += bytesRemaining;
		bytesRemaining = 0;
	} else {
		result = toRead;
		bytesRemaining-=toRead;
	}

	// check for possible last page ogg header
	// i will be location of nextOgg header after the last page
	dataptr = readBuffer;
	outptr = ptr;
	for (i=0;i<result-5;i++) {
		if (memcmp(dataptr,"OggS",4)==0) {
			if ( OggStreamState!=OGG_STATE_LASTPAGE) {
				header = (struct my_oggHeader *) dataptr;
				if ( header->type == 4 || header->type == 5 ) {
					// found last page now wait for next ogg header
					OggStreamState = OGG_STATE_LASTPAGE;
				}
			} else {
				// could be in second pass
				OggStreamState = OGG_STATE_NEWPAGE;
				bytesRemaining += (result - i);
				result = i;
				break;
			}
		}
		*outptr = *dataptr;
		outptr++;
		dataptr++;
	}
	if ( OggStreamState!=OGG_STATE_NEWPAGE) {
		// check for possible split OggS header in last 5 bytes
		outptr--;
		for (;i<result;i++) {
			outptr++;
			if (memcmp(dataptr,"OggS\0",result-i)==0) {
				bytesRemaining += (result-i);
				result = i;
				// most of the time it will be the last element 'O'
				readBuffer[0] = *dataptr;
				break;
			}
			*outptr = *dataptr;
			dataptr++;
		} 
	}
	if (bytesRemaining > 1 ) {
		memcpy(readBuffer,&readBuffer[result],bytesRemaining);
	}
	return result;
}

int ogg_seek_callback(void *datasource, ogg_int64_t offset, int whence) {
	int result = -1;
	return result;
}

int ogg_close_callback(void *datasource) 
{
	// must close oggfile outside of code;
	return 0;
}

long ogg_tell_callback(void *datasource) 
{
	return -1;
}

static int
ogg_setup(void)
{
	int i,rc;

	if (OggStreamState!=OGG_STATE_PLAYNEWPAGE) {
		oggfile = fdopen(fd, "rb");
	} else {
		ov_clear(&vf);
	}

	if (http_playing==HTTP_AUDIO_FILE_OGG ) {
		ov_callbacks ogg_callbacks;
		ogg_callbacks.read_func =  ogg_read_callback;
		ogg_callbacks.close_func = ogg_close_callback;
		ogg_callbacks.seek_func = ogg_seek_callback;
		rc = ov_open_callbacks(oggfile, &vf, NULL, 0,ogg_callbacks);
	} else {
		rc = ov_open(oggfile, &vf, NULL, 0);
	}

	if ( rc < 0 ) {
		fprintf(stderr, "Failed to open Ogg file %s %d\n", current,rc);
		OggStreamState = OGG_STATE_UNKNOWN;
		if (http_playing==HTTP_AUDIO_FILE_OGG) {
			audio_stop = 1;
		}
		return -1;
	}
	vi = ov_info(&vf, -1);
	printf("Bitstream is %d channel, %ldHz\n", vi->channels, vi->rate);


	if (http_playing==HTTP_AUDIO_FILE_OGG) {
		vorbis_comment* comment;
		char artist[30];
		char title[60];
		artist[0]=title[0]=0;
		comment = ov_comment (&vf, -1);
		if (comment != NULL ) {
			if (comment->comments !=0 ) {
				for (i = 0; i < comment->comments; ++i) {
					if (strncasecmp(comment->user_comments[i],"ARTIST=",7)==0 ) {
						snprintf(artist,30,"%s",&comment->user_comments[i][7]);
						if (title[0]!=0) {
							break;
						}
					} else if (strncasecmp(comment->user_comments[i],"TITLE=",6)==0 ) {
						snprintf(title,60,"%s",&comment->user_comments[i][6]);
						if (artist[0]!=0) {
							break;
						}

					}
				}
				char my_display[60];
				if (artist[0]!=0 ) {
					snprintf(my_display,60,"%s - %s",artist,title);
				} else if (title[0]!=0) {
					snprintf(my_display,60,"%s",title);
				} else {
					strcpy(my_display,"missing");
				}
				printf("%s\n",my_display);
				mvpw_set_text_str(fb_name,my_display);
			}
		}
	}

	if (OggStreamState==OGG_STATE_UNKNOWN) {
		av_set_audio_output(AV_AUDIO_PCM);
		if (av_set_pcm_param(vi->rate, 0, vi->channels, 0, 16) < 0) {
			OggStreamState = OGG_STATE_UNKNOWN;
			return -1;
		}
	} else {
		/* hopefully stream should have same bit rate  don't want to kill 
		   audio in the buffer
		*/
	}
	OggStreamState = OGG_STATE_OPEN;
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
	http_playing = HTTP_FILE_CLOSED;
	audio_type = AUDIO_FILE_UNKNOWN;

	if(fd >= 0) {
		close(fd);
		fd = -1;
	}
	if (oggfile != NULL) {
		ov_clear(&vf);
		if (OggStreamState != OGG_STATE_UNKNOWN) {
			fclose(oggfile);
		}
		oggfile = NULL;
	}
	if ( using_helper > 0 || using_vlc == 1 ) {
		http_playing = HTTP_FILE_CLOSED;
		FILE *outlog;
		outlog = fopen("/usr/share/mvpmc/connect.log","a");
		if ( using_helper == 1 ) {
			mplayer_helper_connect(outlog,NULL,1);
		}
		vlc_connect(outlog,NULL,1,VLC_DESTROY,NULL, 0);
		using_vlc = 0;
		usleep(3000);
		if ( using_helper == 1 ) {
			mplayer_helper_connect(outlog,NULL,2);
		}
		using_helper=0;
		fclose(outlog);
		mvpw_hide(fb_progress);
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
wav_play(int afd)
{
	unsigned char *wav_buffer;
	unsigned char pcm_buffer[BUFFSIZE][4];
	int n;
	int input_ptr;
	int iloop;
	int sample_value = 0;
	int empty = 0;
	static unsigned long quantised_next_input_sample = 0;
	static unsigned long last_sample_in_buffer = 0;
	static unsigned long next_output_sample = 0;
	static unsigned long next_input_sample = 0;
	static unsigned wav_file_to_input_frequency_ratio = 1;

	if (afd==-1){
		empty_ac3();
		quantised_next_input_sample = 0;
		last_sample_in_buffer = 0;
		next_output_sample = 0;
		next_input_sample = 0;
		return -1;
	}

	if (ac3_flush() == 0)
		empty = 1;

	if (ac3_freespace() < (BUFFSIZE*align)) {
		usleep(1000);
		return 0;
	}

	wav_buffer = alloca(align * BUFFSIZE);
	errno = 0;
	do {
		n = read(fd, wav_buffer, min((align * BUFFSIZE), chunk_size));
	} while ( n < 0 && (errno==EAGAIN || errno == EINTR));

	if (n < 0) {
		return -1;
	} else if ((n == 0) && empty){
		return 1;
	}

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
				sample_value = (wav_buffer[input_ptr + (iloop * bps) + bps - 1 ] <<8 ) +
				 wav_buffer[input_ptr + (iloop * bps) + bps - 2 ];
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
	static int old_audio_type = AUDIO_FILE_UNKNOWN;

	if (gui_state != MVPMC_STATE_EMULATE) {
		if ( is_streaming(current) < 0    ) {
			if ((fd=open(current, O_RDONLY|O_LARGEFILE|O_NDELAY)) < 0) {
				goto fail;
			}
		}
		audio_type=get_audio_type(current);
	} else {
		audio_type=AUDIO_FILE_MVP_MP3;
		fd = open("/tmp/FIFO", O_RDONLY);
	}
	switch (audio_type) {
	case AUDIO_FILE_MVP_MP3:
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
	case AUDIO_FILE_FLAC:
	case AUDIO_FILE_HTTP_FLAC:
		av_set_audio_output(AV_AUDIO_PCM);
		av_set_pcm_param(44100, 0, 2, 0,16);
		break;
	case AUDIO_FILE_UNKNOWN:
		goto fail;
		break;
	}

	av_play();

	if (old_audio_type != audio_type ) {
		while ( av_empty()==0 ) {
			// empty audio buffer
			usleep(10000);
		}
		old_audio_type = audio_type;
	}

	switch (audio_type) {
	case AUDIO_FILE_WAV:
		wav_play(-1);
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
	case AUDIO_FILE_MVP_MP3:
		mp3_play(-1);
		break;
	case AUDIO_FILE_HTTP_MP3:
		http_play(-1);
		break;
	case AUDIO_FILE_FLAC:
	case AUDIO_FILE_HTTP_FLAC:
		if (flac_play(-1) < 0)
			goto fail;
		break;
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

void mvpw_load_image_fd(int remotefd);


/*
 * audio_start() - audio playback thread
 */
void*
audio_start(void *arg)
{
	sigset_t sigs;
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	int afd;
	int done=0;

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
		
        	afd = av_get_audio_fd();

		audio_playing = 1;
		audio_stop = 0;
		OggStreamState = OGG_STATE_UNKNOWN;

		mvpw_show(fb_progress);

		while (( (done=audio_player(0, afd)) == 0)  &&
		       current && !audio_stop)
			;

		if (http_playing == HTTP_VIDEO_FILE_MPG ) {
			continue;
		} else if (http_playing == HTTP_AUDIO_FILE_OGG ||
		    	http_playing == HTTP_AUDIO_FILE_FLAC ){
			goto repeat;
		}

	fail:
		if (!audio_stop && playlist) {
			if (is_streaming(current) != 0 ) {
				close(fd);
				fd = -1;
			}
			playlist_next();
			if (playlist) {
				printf("next song on playlist\n");
				goto repeat;
			}
		}

		printf("Done with audio file\n");

		close(fd);
		fd = -1;
		audio_clear();
		if (done < 0 ) {
			mvpw_hide(fb_progress);
		}
	}
	return NULL;
}

void urldecode( char* to, char* from );

static int http_play(int afd)
{
	int rc = 0;
	char *ptr;
	if (afd==-1) {
		if (current  && is_streaming(current)>=0) {
			if (strstr(cwd,"/uPnP")!=NULL) {
				char *newcurrent;
				newcurrent = strdup(current);
				urldecode(newcurrent,current);
				free(current);
				current = strdup(newcurrent);
				urldecode(current,newcurrent);
				free(newcurrent);
			}
			if (strlen(current) < MAX_URL_LEN ) {
				rc = 0;
				mvpw_set_timer(playlist_widget, content_osd_update, 500);
			} else {
				rc = -1;
			}
		} else {
			rc = -1;
		}
	} else {
		if (current && is_streaming(current)>=0 ) {
			ptr = strpbrk (current,"\r\n");
			if (ptr!=NULL) {
				*ptr=0;
			}
			mvpw_set_text_str(fb_name, current);
			http_playing = http_main();

			mvpw_set_timer(playlist_widget, NULL, 0);

			switch (http_playing) {
			case HTTP_FILE_CLOSED:
			case HTTP_FILE_ERROR:
			case HTTP_FILE_UNKNOWN:
			default:
				audio_type = AUDIO_FILE_UNKNOWN;
				http_playing = HTTP_FILE_CLOSED;
				rc = -1;
				break;
			case HTTP_AUDIO_FILE_OGG:
				audio_type = AUDIO_FILE_HTTP_OGG;
				rc = 0;
				break;
			case HTTP_VIDEO_FILE_MPG:
				av_reset();
				mvpw_hide(fb_progress);
				if (mvpw_visible(playlist_widget)) {
					mvpw_hide(playlist_widget);
				}
				mvpw_hide(file_browser);
				video_thumbnail(0);
				video_set_root();
				mvpw_focus(root);
				screensaver_disable();
				mvpw_set_timer(root, video_play, 50);
				rc = -1;
				break;
			case HTTP_AUDIO_FILE_FLAC:
				audio_type = AUDIO_FILE_HTTP_FLAC;
				rc = 0;
				break;
			case HTTP_IMAGE_FILE_JPEG:
				audio_type = AUDIO_FILE_UNKNOWN;
				mvpw_hide(fb_progress);
				if (mvpw_visible(playlist_widget)) {
					mvpw_hide(playlist_widget);
				}
				printf("Displaying image '%s'\n", current);
				mvpw_load_image_fd(fd);
				if (mvpw_load_image_jpeg(iw, NULL) == 0) {
					mvpw_show(iw);
					mvpw_show_image_jpeg(iw);
					av_wss_update_aspect(WSS_ASPECT_UNKNOWN);
					while (audio_stop==0) {
						usleep(250000);
					}
					mvpw_hide(iw);
				}
				mvpw_show(playlist_widget);
				mvpw_focus(playlist_widget);
				http_playing = HTTP_FILE_CLOSED;
				rc = 1;
				break;
			}
		} else {
			rc = -1;
		}
	}
	return rc;
}

#ifndef MVPMC_HOST

#include "FLAC/stream_decoder.h"

typedef enum {
	LAYER_STREAM = 0, /* FLAC__stream_decoder_init_[ogg_]stream() without seeking */
	LAYER_SEEKABLE_STREAM, /* FLAC__stream_decoder_init_[ogg_]stream() with seeking */
	LAYER_FILE, /* FLAC__stream_decoder_init_[ogg_]FILE() */
	LAYER_FILENAME /* FLAC__stream_decoder_init_[ogg_]file() */
} Layer;

typedef struct {
	unsigned bits_per_sample; /* bits per sample */
	unsigned sample_rate; /* samples per second (in a single channel) */
	unsigned channels; /* number of audio channels */
	int byte_format; /* Byte ordering in sample */
} ao_sample_format;

typedef struct {
	Layer layer;
	FILE *file;
	FLAC__bool ignore_errors;
	FLAC__bool error_occurred;
	int yield_samples;
	ao_sample_format sam_fmt; /* input sample's true format */
	int afd;
} StreamDecoderClientData;


#define FLAC_WRITE_YIELD_FREQ 5


static FLAC__StreamDecoderWriteStatus stream_decoder_write_callback_(const FLAC__StreamDecoder *decoder, 		
	const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data)
{
	StreamDecoderClientData *dcd = (StreamDecoderClientData*)client_data;

	(void)decoder, (void)buffer;
	if ( audio_stop != 0 ) {
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}

	if (0 == dcd) {
		printf("ERROR: client_data in write callback is NULL\n");
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}

	if (dcd->error_occurred)
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

	if (
	   (frame->header.number_type == FLAC__FRAME_NUMBER_TYPE_FRAME_NUMBER && frame->header.number.frame_number == 0) ||
	   (frame->header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER && frame->header.number.sample_number == 0)
	   ) {
		printf("Found FLAC content...");
		fflush(stdout);
	}

	if (dcd->afd == -1) {
		return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;        
	}

	FLAC__uint32 samples = frame->header.blocksize;
	FLAC__uint32 decoded_size = frame->header.blocksize * frame->header.channels * (dcd->sam_fmt.bits_per_sample / 8 );
	static FLAC__uint8 aobuf[FLAC__MAX_BLOCK_SIZE * FLAC__MAX_CHANNELS * sizeof(FLAC__uint8)]; /*oink!*/
	FLAC__uint8   *u8aobuf = (FLAC__uint8  *) aobuf;
	FLAC__uint16 *u16aobuf = (FLAC__uint16 *) aobuf;
	int sample, channel, i=0;

	if (dcd->sam_fmt.bits_per_sample == 8) {
		for (sample = i = 0; sample < samples; sample++) {
			for (channel = 0; channel < frame->header.channels; channel++,i++) {
				u8aobuf[i] = buffer[channel][sample];
			}
		}
	} else if (dcd->sam_fmt.bits_per_sample == 16) {
		for (sample = i = 0; sample < samples; sample++) {
			for (channel = 0; channel < frame->header.channels; channel++,i++) {
				u16aobuf[i] = (FLAC__uint16)(buffer[channel][sample]);
			}
		}
	}

	int pos = 0,len=0;
	while (decoded_size > 0 && audio_stop == 0) {
		if ((len = write(dcd->afd, aobuf + pos, decoded_size)) == -1) {
			fprintf(stderr,"Error during audio write\n");
			return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
		}
		if (len == 0) {
			usleep(100000);
		} else {
			decoded_size-=len;
			pos+=len;
		}
	};
	if (dcd->yield_samples < 0) {
		if (audio_type == AUDIO_FILE_FLAC ){
			FLAC__uint64 whereami=0;
			FLAC__stream_decoder_get_decode_position(decoder,&whereami);
			bytesRead = whereami;
		} else {
			bytesRead+=len;
		}
		usleep(10000);
		dcd->yield_samples = dcd->sam_fmt.sample_rate/FLAC_WRITE_YIELD_FREQ;
	} else {
		dcd->yield_samples -= samples;
	}
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void stream_decoder_error_callback_(const FLAC__StreamDecoder *decoder, 
	FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	StreamDecoderClientData *dcd = (StreamDecoderClientData*)client_data;

	(void)decoder;

	if (0 == dcd) {
		printf("ERROR: client_data in error callback is NULL\n");
		return;
	}

	if (!dcd->ignore_errors) {
		printf("ERROR: got error callback: err = %u(%s)\n",
		       (unsigned)status,FLAC__StreamDecoderErrorStatusString[status]);
		dcd->error_occurred = true;
	}
}

int flac_play(int afd)
{
	int rc;
	FLAC__StreamDecoderState state;
	static FLAC__StreamDecoder *decoder = NULL;
	static StreamDecoderClientData decoder_client_data;
	if (afd == -1) {
		bytesRead = 0;
		if (audio_type == AUDIO_FILE_FLAC ){
			struct stat64 sb;
			if (fstat64(fd, &sb) < 0)
				return -1;	
			contentLength = sb.st_size;
		}
		mvpw_set_timer(fb_progress, content_osd_update, 500);

		decoder = FLAC__stream_decoder_new();
		decoder_client_data.layer = LAYER_FILE;
		decoder_client_data.afd = afd;
		decoder_client_data.ignore_errors = false;
		decoder_client_data.error_occurred = false;
		decoder_client_data.file = fdopen(fd,"rb");

//		rc = FLAC__stream_decoder_init_file(decoder,current,stream_decoder_write_callback_,0,
		rc = FLAC__stream_decoder_init_FILE(decoder,decoder_client_data.file,stream_decoder_write_callback_,0,
						stream_decoder_error_callback_,&decoder_client_data);

		if ( rc != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
			printf("Could not init libFLAC %d\n",rc);
			FLAC__stream_decoder_delete(decoder);
			return -1;
		}

		printf ("libFLAC init %s\n",current);

		state = FLAC__stream_decoder_get_state(decoder);
		printf("Returned state = %u (%s)... OK\n", state, FLAC__StreamDecoderStateString[state]);

		printf("FLAC__stream_decoder_process_until_end_of_metadata()\n");
		if (!FLAC__stream_decoder_process_until_end_of_metadata(decoder)) {
			FLAC__stream_decoder_delete(decoder);
			return -1;
		}

		state = FLAC__stream_decoder_get_state(decoder);
		printf("Returned state = %u (%s)... OK\n", state, FLAC__StreamDecoderStateString[state]);
		printf("Skip single frame  FLAC__stream_decoder_skip_single_frame()...");
		if (!FLAC__stream_decoder_skip_single_frame(decoder)) {
			state = FLAC__stream_decoder_get_state(decoder);
			printf("Returned state = %u (%s)... OK\n", state, FLAC__StreamDecoderStateString[state]);
			FLAC__stream_decoder_delete(decoder);
			return -1;
		}
		printf("OK\nFLAC__stream_decoder_get_channels()... ");
		decoder_client_data.sam_fmt.channels = FLAC__stream_decoder_get_channels(decoder);
		printf ("%d\n",decoder_client_data.sam_fmt.channels);

		printf("FLAC__stream_decoder_get_bits_per_sample()... ");
		decoder_client_data.sam_fmt.bits_per_sample = FLAC__stream_decoder_get_bits_per_sample(decoder);
		printf ("%d\n",decoder_client_data.sam_fmt.bits_per_sample);

		printf("FLAC__stream_decoder_get_sample_rate()... ");
		decoder_client_data.sam_fmt.sample_rate = FLAC__stream_decoder_get_sample_rate(decoder);
		printf ("%d\n",decoder_client_data.sam_fmt.sample_rate);

		decoder_client_data.yield_samples = decoder_client_data.sam_fmt.sample_rate/FLAC_WRITE_YIELD_FREQ;

		if (decoder_client_data.sam_fmt.sample_rate != 44100  || decoder_client_data.sam_fmt.channels != 2 || decoder_client_data.sam_fmt.bits_per_sample != 16 ) {
			if ( av_set_pcm_param(decoder_client_data.sam_fmt.sample_rate,0, decoder_client_data.sam_fmt.channels,0,decoder_client_data.sam_fmt.bits_per_sample) < 0) {
				FLAC__stream_decoder_delete(decoder);
				return -1;
			}
		}
		printf("FLAC__stream_decoder_get_blocksize()... ");
		{
			unsigned blocksize = FLAC__stream_decoder_get_blocksize(decoder);
			/* value could be anything since we're at the last block, so accept any reasonable answer */
			printf("returned %u... %s\n", blocksize, blocksize>0? "OK" : "FAILED");
			if (blocksize == 0) {
				FLAC__stream_decoder_delete(decoder);
				return -1;
			}
		}
		rc = 0;
	} else {
		decoder_client_data.afd = afd;
//		might need FLAC__stream_decoder_flush(decoder);
		if (audio_type == AUDIO_FILE_FLAC ){
			printf("Reset using FLAC__stream_decoder_seek_absolute()\n ");
			FLAC__stream_decoder_seek_absolute(decoder, 0);
			state = FLAC__stream_decoder_get_state(decoder);
			printf("Returned state = %u (%s)... OK\n", state, FLAC__StreamDecoderStateString[state]);
		}
		printf("FLAC__stream_decoder_process_until_end_of_stream()... ");
		if (!FLAC__stream_decoder_process_until_end_of_stream(decoder)) {
			if (audio_stop == 0 ) {
				printf("stream error\n");
			} else {
				printf("remote stop\n");
			}
		} else {
			while ( av_empty()==0 ) {
				// empty audio buffer 
				usleep(10000);
			}
			printf("stream complete %d\n",errno);
		}
		if (!FLAC__stream_decoder_finish(decoder)) {
		}
		FLAC__stream_decoder_delete(decoder);
		http_playing = HTTP_FILE_CLOSED;
		rc = -1;
	}
	return rc;
}
#else
int flac_play(int afd)
{
	return -1;
}
#endif
