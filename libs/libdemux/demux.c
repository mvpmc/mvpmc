/*
 *  Copyright (C) 2004-2006 Jon Gettler
 *  http://www.mvpmc.org/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * This file contains the entry points to the demux library.  The library
 * is not threaded, but it is thread safe as long as the following rules
 * are followed:
 *
 *	1) simultaneous calls to demux_put() are not safe
 *	2) simultaneous calls the demux_get/demux_write routines on the
 *	   same stream are not safe
 *	3) demux_buffer_resize() must not be called at the same time as
 *	   any other demux library functions
 *
 * If an application might break these rules, locking should be used to
 * avoid undefined behaviour.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mvp_demux.h"
#include "mvp_av.h"
#include "demux.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

#if 0
#define JIT_PRINTF(x...) printf(x)
#else
#define JIT_PRINTF(x...) PRINTF(x)
#endif

/*
 * demux_init() - Create and initialize a demux context
 *
 * Arguments:
 *	size	- amount of memory that the demuxer is allowed to use
 *
 * Returns:
 *	pointer to demux context
 */
demux_handle_t*
demux_init(unsigned int size)
{
	demux_handle_t *handle;

	if ((handle=malloc(sizeof(*handle))) == NULL)
		return NULL;
	memset(handle, 0, sizeof(*handle));

	handle->size = size;
	handle->spu_current = -1;

	handle->attr.audio.current = -1;
	handle->attr.video.current = -1;

	demux_reset(handle);

	if (start_stream(handle) < 0)
		return NULL;

	return handle;
}

int
demux_destroy(demux_handle_t *handle)
{
	if (handle == NULL)
		return -1;

	if(handle->stream_buf)
		free(handle->stream_buf);
	free(handle);

	return 0;
}

/*
 * demux_put() - Add a media stream buffer to the demuxer
 *
 * Arguments:
 *	handle	- pointer to demux context
 *	buf	- buffer containing media stream data
 *	len	- amount of data in the buffer
 *
 * Returns:
 *	amount of data consumed
 */
int
demux_put(demux_handle_t *handle, void *buf, int len)
{
	int n, ret = 0;

	if (handle == NULL)
		return -1;

	if (buf == NULL)
		return -1;

	if (len < 0)
		return -1;

 loop:
	PRINTF("demux_put(): state %x len %d\n", handle->state, len);
	if (len == 0)
		return 0;

	switch (handle->state) {
	case 1 ... 4:
		/* frame header */
		ret += add_buffer(handle, buf, len);
        PRINTF("demux_put(): added %d of %d bytes state %x\n", ret, len,handle->state);
		break;
	case MPEG_program_end_code:
	case pack_start_code:
	case system_header_start_code:
	case user_data_start_code:
	case video_stream_0 ... video_stream_F:
	case audio_stream_0 ... audio_stream_F:
	case private_stream_2:
	case private_stream_1:
    case padding_stream:
    case 0xb3:
    case 0xb5:
        
		/* middle of frame */
		n = parse_frame(handle, buf, len, handle->state);
		PRINTF("demux_put(): parsed %d of %d bytes\n", n, len);
		ret += n;
		buf += n;
		len -= n;
		if (handle->state == 1)
			goto loop;
		break;
	default:
		ret = -1;
		PRINTF("demux_put(): parsed %d of %d bytes state %x\n", -1, len,handle->state);
		break;
	}

	return ret;
}

/*
 * demux_get_audio() - Get audio data out of the demuxer
 *
 * Arguments:
 *	handle	- pointer to demux context
 *	buf	- buffer to place audio data in
 *	max	- maximum bytes to place in buffer
 *
 * Returns:
 *	number of bytes returned
 */
int
demux_get_audio(demux_handle_t *handle, void *buf, int max)
{
	if (handle == NULL)
		return -1;
	if (max <= 0)
		return -1;
	if (handle->audio == NULL)
		return 0;

	return stream_drain(handle->audio, buf, max);
}

/*
 * demux_get_video() - Get video data out of the demuxer
 *
 * Arguments:
 *	handle	- pointer to demux context
 *	buf	- buffer to place video data in
 *	max	- maximum bytes to place in buffer
 *
 * Returns:
 *	number of bytes returned
 */
int
demux_get_video(demux_handle_t *handle, void *buf, int max)
{
	if (handle == NULL)
		return -1;
	if (max <= 0)
		return -1;
	if (handle->video == NULL)
		return 0;

	return stream_drain(handle->video, buf, max);
}

/*
 * demux_write_audio() - Write audio data to a file descriptor
 *
 * Arguments:
 *	handle	- pointer to demux context
 *	fd	- file descriptor for writing
 *
 * Returns:
 *	number of bytes written
 */
int
demux_write_audio(demux_handle_t *handle, int fd)
{
	if (handle == NULL)
		return -1;
	if (handle->audio == NULL)
		return 0;

	return stream_drain_fd(handle->audio, fd,-1);
}

static inline int
demux_pts_in_window(unsigned int window_start, unsigned int window_end,
		    unsigned int check)
{
    /*Must handle the case when window_end has wrapped around so that the
     *value appears to be less than window_start
     */
    if(window_end > window_start)
	return (check >= window_start && check <= window_end);
    else
	return (check >= window_start || check <= window_end);
}

/*
 * demux_jit_write_audio() - Write audio data to a file descriptor Just In Time
 *
 * Arguments:
 *	handle	- pointer to demux context
 *	fd	- file descriptor for writing
 *	mode    - a bitwise or of the following flags:
 *	           1 - Bring audio in sync after a seek by throwing some away
 *	           2 - Try to keep audio in sync at other times
 *	stc     - current video STC (32 LSBits of it anyway)
 *		   (only needed for mode 2)
 *	flags	- Indicate any A/V action that must be performed:
 *		  1 - video_pause
 *		  2 - video_unpause (after duration)
 *		  4 - video_pause_duration - Pause the video for the specified
 *                                   duration.
 *		  8 - audio_stall - Wait a while before re-trying to send audio
 *	duration - Duration of any wait for video_unpause/audio_stall in
 *	            milliseconds
 *
 * Returns:
 *	number of bytes written
 */
int
demux_jit_write_audio(demux_handle_t *handle, int fd, unsigned int stc, int mode, int *flags, int *duration)
{
        (*flags) = 0;
	(*duration) = 0;

	/* If neither "JIT" function is enabled, then just let the normal
	 * demux_write handle it. If we're handling doing post-seek audio sync
	 * then do a normal demux_write if we're not just post-sync.*/
	if (mode == 0 || (mode == 1 && handle->jit.seek_end_pts == 0))
	    return demux_write_audio(handle,fd);

	/* Sanity check: */
	if (handle == NULL)
		return -1;
	if (handle->audio == NULL)
		return 0;

	/* No left over audio from a previous attempt to send/discard a frame?
	 * Start processing a new frame then.*/
	if(handle->jit.frame_remain <= 0)
	{
	    unsigned char buf[14];
	    int pack_len = 0;
	    int resync = 0;
	    /* Try and see if we can find a header */
	    int len = stream_peek(handle->audio,buf,14);

	    /* Didn't get enough bytes to read a header? Then give up and
	     * try again later */
	    if(len < 8) 
		return 0;
	
	    while(buf[0] != 0 || buf[1] != 0 || buf[2] != 1)
	    {
		/* Not synced, so move forward one byte at a time until we hit
		 * the start of a frame */
		char tmp;
		stream_drain(handle->audio,&tmp,1);
		resync++;
		len = stream_peek(handle->audio,buf,14);
		if(len < 8 )
		{
		    JIT_PRINTF("JIT Audio lost sync, haven't regained it yet after moving %d bytes\n",resync);
		    return 0;
		}
	    }

	    /* Warn if we had to re-sync, there's either an erronous stream, or
	     * a bug in this code */
	    if(resync != 0)
	    {
		JIT_PRINTF("JIT Audio lost sync, had to move %d bytes to resync\n",resync);
	    }
	    /*See http://dvd.sourceforge.net/dvdinfo/pes-hdr.html for info
	     * on MPEG PES headers
	     */
	    pack_len = buf[4] << 8 | buf[5];
	    /*Check if there's a PTS present:*/
	    if((buf[7] & 0x80) != 0)
	    {
		unsigned int audio_pts;
		if(len < 14)
		    return 0;
		/* Ignore MSB to keep this in a 32bit int */
		audio_pts = (buf[13] >> 1) | (buf[12] << 7)
			     | ((buf[11] & 0xFE) << 14) | (buf[10] << 22)
			     | ((buf[9] & 0x6) << 29);
		/* If we have a seek end pts then we just throw everything
		 * away until we get to that PTS
		 */
		/*Assume that anything close (30secs) to the PTS is synced
		 *otherwise assume audio is thoroughly non-sync anyway, so just
		 *let it past
		 */
		JIT_PRINTF("JIT Audio: Frame has audio_pts: %X, seek_end_pts: %X, stc: %X\n",audio_pts, handle->jit.seek_end_pts,stc);

		if((mode & 1) && handle->jit.seek_end_pts != 0)
		{
		    unsigned int window_start = handle->jit.seek_end_pts - 30*PTS_HZ;
		    /*If it's .3 second early then we'll send it out*/
		    unsigned int window_end = handle->jit.seek_end_pts - PTS_HZ*0.3;
		    JIT_PRINTF("JIT Audio: seek pts window_start: %X, window_end: %X\n", window_start, window_end);

		    /*If our audio is outside the window then clear
		     * seek_end_pts, allowing data to go out
		     */
		    if(!demux_pts_in_window(window_start,window_end,audio_pts))
		    {
			handle->jit.ignore_frame_pts = 2;
			(*flags) |= 2;/*Trigger video un-pause after any duration:*/

			if(demux_pts_in_window(window_end,handle->jit.seek_end_pts,audio_pts))
			{
			    /*Not quite there yet, send through the audio
			     * but pause the video so it can start playing
			     * at the right time
			     */
			    (*flags) |= 1;/*Trigger video pause*/

			    /*Duration after which the unpause will be done*/
			    (*duration) = (handle->jit.seek_end_pts - audio_pts)/PTS_kHz;
			    if((*duration) > 150)
				(*duration) -=150;
			    else
				(*duration) = 0;
			    handle->jit.ignore_frame_pts++;
			}
			handle->jit.seek_end_pts = 0;
			JIT_PRINTF("JIT Audio in range after seek, unpausing video after %dms\n",*duration);
		    }
		    else
		    {
			JIT_PRINTF("JIT Audio out of range after seek, pausing video\n");
			handle->jit.ignore_frame_pts = 0;
			(*flags) |= 1;/*Trigger video pause*/
		    }
		}
		else if(handle->jit.ignore_frame_pts > 0)
		{
		    handle->jit.ignore_frame_pts--;
		}
		else if(mode & 2)/* We aren't doing handling just after a seek so do normal "JIT" audio handling */
		{
		    unsigned int windows[4];
		    /*We'd always like the PTS of the audio we're delivering
		     * to be greater than the current video pts:
		     */
		    /*If the Audio PTS isn't between windows[0] and windows[3],
		     * then assume it's totally unsynced and just let it pass
		     */
		    windows[0] = stc - 20*PTS_HZ; /*Video ahead of audio*/
		    windows[1] = stc + 0.1*PTS_HZ; /*Beginning of "valid" window*/
		    windows[2] = stc + 2*PTS_HZ; /*End of "valid" window*/
		    windows[3] = stc + 20*PTS_HZ; /*Audio getting ahead of video*/
		    if(demux_pts_in_window(windows[0],windows[1],audio_pts))
		    {
			(*flags) |= 4;
			(*duration) = (windows[1] - audio_pts)/PTS_kHz;
			JIT_PRINTF("JIT Audio: Video leading audio, want to trigger video pause for %dms\n", *duration);
			if((*duration) > 500)
			{
			    (*duration) = 500;
			    JIT_PRINTF("JIT Audio: Only allowing video delay of %dms",(*duration));
			}
		    }
		    else if(demux_pts_in_window(windows[2],windows[3],audio_pts))
		    {
			JIT_PRINTF("JIT Audio: Audio buffer leading video, waiting to send audio\n");
			(*flags) |= 8;
			(*duration) = 10;
			return 0;
		    }
		}
		JIT_PRINTF("JIT Audio: Found packet of length %d\n",pack_len);
		handle->jit.frame_remain = pack_len + 6;
	    } else {
		handle->jit.frame_remain = pack_len + 6;
	    }
	}

	if(handle->jit.frame_remain > 0)
	{
	    int sent;
	    if(handle->jit.seek_end_pts != 0)
	    {
		JIT_PRINTF("JIT Audio: Dumping frame of audio because we've just seeked\n");
		sent = stream_drain(handle->audio, NULL, handle->jit.frame_remain);
	    }
	    else
	    {
		sent = stream_drain_fd(handle->audio, fd, handle->jit.frame_remain);
	    }
	    handle->jit.frame_remain -= sent;
	    JIT_PRINTF("JIT Audio: Wrote %d bytes to the audio fd\n",sent);
	    return sent;
	}
	return 0;
}

/*
 * demux_write_video() - Write video data to a file descriptor
 *
 * Arguments:
 *	handle	- pointer to demux context
 *	fd	- file descriptor for writing
 *
 * Returns:
 *	number of bytes written
 */
int
demux_write_video(demux_handle_t *handle, int fd)
{
	if (handle == NULL)
		return -1;
	if (handle->video == NULL)
		return 0;

	return stream_drain_fd(handle->video, fd, -1);
}

/*
 * demux_get_attr() - Get demux attribute structure
 *
 * Arguments:
 *	handle	- pointer to demux context
 *
 * Returns:
 *	pointer to the demux attribute structure
 */
demux_attr_t*
demux_get_attr(demux_handle_t *handle)
{
	return &handle->attr;
}

#if 0
/* TODO: Fix stream_buffer_resize so that it works with all the new tails.
 */
/*
 * demux_buffer_resize() - resize audio and video buffers to fit stream profile
 *
 * Arguments:
 *	handle	- pointer to demux context
 *
 * Returns:
 *	1 if buffers were changed, 0 if they were not
 */
int
demux_buffer_resize(demux_handle_t *handle)
{
	float buf_ratio, byte_ratio;

	if ((handle->audio->size == 0) ||
	    (handle->audio->attr->stats.bytes == 0))
		return 0;

	buf_ratio = (float)handle->video->size / handle->audio->size;
	byte_ratio = (float)handle->video->attr->stats.bytes /
		handle->audio->attr->stats.bytes;

	if (byte_ratio > buf_ratio) {
		float target = byte_ratio * 0.60;

		if (target != buf_ratio) {
			float size;
			int asize, vsize;

			size = handle->video->size + handle->audio->size;
			asize = size / (target + 0);
			vsize = size - asize;

			PRINTF("bytes audio %d video %d\n",
			       handle->audio->attr->stats.bytes,
			       handle->video->attr->stats.bytes);
			PRINTF("ratio buf %5.2f byte %5.2f target %5.2f\n",
			       buf_ratio, byte_ratio, target);
			PRINTF("size %5.2f resize to audio %d video %d\n",
			       size, asize, vsize);

			if (stream_resize(handle->audio,
					  handle->audio->buf +
					  handle->audio->size - asize,
					  asize) == 0) {
				stream_resize(handle->video,
					      handle->video->buf,
					      vsize);
				handle->attr.audio.bufsz = asize;
				handle->attr.video.bufsz = vsize;
				return 1;
			} else {
				PRINTF("buffer resize failed\n");
			}
		}
	}

	return 0;
}
#endif

/*
 * demux_empty() - see if the demux buffers are empty
 *
 * Arguments:
 *	handle	- pointer to demux context
 *
 * Returns:
 *	1 if the buffers are empty, and 0 if they are not empty
 */
int
demux_empty(demux_handle_t *handle)
{
	if (handle->audio &&
	    (handle->audio->head !=
	     ((handle->audio->tail + 1) % handle->audio->size)))
		return 0;

	if (handle->video &&
	    (handle->video->head !=
	     ((handle->video->tail + 1) % handle->video->size)))
		return 0;

	return 1;
}

/*
 * demux_flush() - flush media data from the demux buffers
 *
 * Arguments:
 *	handle	- pointer to demux context
 *
 * Returns:
 *	0 for success
 */
int
demux_flush(demux_handle_t *handle)
{
	PRINTF("%s()\n", __FUNCTION__);

	if (handle->video) {
		handle->video->head = 0;
		handle->video->tail = handle->video->parser_tail = handle->video->size - 1;
		handle->video->attr->stats.cur_bytes = 0;
	}

	if (handle->audio) {
		handle->audio->buf = handle->video->buf + handle->video->size;
		handle->audio->head = 0;
		handle->audio->tail = handle->audio->parser_tail = handle->audio->size - 1;
		handle->audio->attr->stats.cur_bytes = 0;
	}
        handle->jit.frame_remain = 0;
	handle->jit.seek_end_pts = 0;
	handle->jit.ignore_frame_pts = 0;
	handle->seeking = 1;

	return 0;
}

int
demux_attr_reset(demux_handle_t *handle)
{
	int i;

	for (i=0; i<SPU_MAX; i++) {
		handle->attr.spu[i].bytes = 0;
		handle->attr.spu[i].frames = 0;
	}

	if (handle->video) {
		handle->video->attr->existing = 0;
		handle->video->attr->current = -1;
		handle->video->attr->stats.cur_bytes = 0;
	}

	if (handle->audio) {
		handle->audio->attr->existing = 0;
		handle->audio->attr->current = -1;
		handle->audio->attr->stats.cur_bytes = 0;
	}

	return 0;
}

int
demux_reset(demux_handle_t *handle)
{
	int i, j;
	int nv, na;

	na = handle->size / 5;
	nv = handle->size - na;

	if (handle->video) {
		handle->attr.video.bufsz = nv;
		handle->video->size = nv;

		handle->video->head = 0;
		handle->video->seeking_head_valid = 0;
		handle->video->tail = handle->video->parser_tail = handle->video->size - 1;
	}

	if (handle->audio) {
		handle->attr.audio.bufsz = na;
		handle->audio->size = na;

		handle->audio->buf = handle->video->buf + handle->video->size;
		handle->audio->head = 0;
		handle->audio->seeking_head_valid = 0;
		handle->audio->tail = handle->audio->parser_tail = handle->audio->size - 1;
	}

	for (i=0; i<SPU_MAX; i++) {
		if (handle->spu[i].buf)
			free(handle->spu[i].buf);
		for (j=0; j<32; j++) {
			if (handle->spu[i].item[j].data)
				free(handle->spu[i].item[j].data);
			memset(handle->spu[i].item+j, 0,
			       sizeof(handle->spu[i].item[j]));
		}
		memset(handle->spu+i, 0, sizeof(handle->spu[i]));
	}

	/*
	 * XXX: reset stats...
	 */

	handle->jit.frame_remain = 0;
	handle->jit.seek_end_pts = 0;
	handle->jit.ignore_frame_pts = 0;
	handle->seeking = 1;
	handle->spu_current = -1;

	return 0;
}

/*
 * demux_seek() - Put the demuxer into seek mode, looking for the next i-frame
 *
 * Arguments:
 *	void
 *
 * Returns:
 *	void
 */
void
demux_seek(demux_handle_t *handle)
{
	handle->seeking = 1;
	handle->jit.frame_remain = 0;
	handle->jit.seek_end_pts = 0;
	handle->jit.ignore_frame_pts = 0;
	handle->attr.gop_valid = 0;
}

/*
 * demux_spu_set_id() - set the current subpicture stream id
 *
 * Arguments:
 *	handle	- pointer to demux context
 *	id	- stream id (-1 for none)
 *
 * Returns:
 *	0	success
 *	-1	invalid stream id
 */
int
demux_spu_set_id(demux_handle_t *handle, int id)
{
	int old = handle->spu_current;
	int i,j;

	if ((id < -1) || (id > 31))
		return -1;

	if (id != old) {
		if (old != -1) {
			handle->spu[old].inuse = 0;
			i = 0;
			while ((i < 32) && handle->spu[old].item[i].data)
				i++;
			if (i) {
				free(handle->spu[old].item[0].data);
				for (j=0; j<i; j++) {
					memcpy(handle->spu[old].item+j,
					       handle->spu[old].item+j+1,
					       sizeof(handle->spu[old].item[j]));
				}
			}
		}

		handle->spu_current = id;
		handle->spu[id].inuse = 1;
	}

	return 0;
}

int
demux_spu_get_id(demux_handle_t *handle)
{
	return handle->spu_current;
}

/*
 * demux_spu_get_next() - get next subpicture for the current stream
 *
 * Arguments:
 *	handle	- pointer to demux context
 *
 * Returns:
 *	an spu item if one is found
 *	NULL if no spu item is found
 */
spu_item_t*
demux_spu_get_next(demux_handle_t *handle)
{
	int i;
	spu_t *spu;
	spu_item_t *ret = NULL;

	if (handle->spu_current < 0)
		return NULL;

	spu = &handle->spu[handle->spu_current];

	if (spu->item[0].data) {
		if ((ret=malloc(sizeof(*ret))) == NULL)
			return NULL;
		memcpy(ret, &spu->item[0], sizeof(*ret));
		for (i=0; i<31; i++)
			memcpy(spu->item+i, spu->item+i+1,
			       sizeof(spu->item[i]));
		memset(spu->item+31, 0, sizeof(spu->item[31]));
	}

	return ret;
}

static unsigned char
get_nibble(unsigned char *buf, int *i, int *state)
{
	unsigned char c;
	int n = *i;

	if (*state) {
		c = buf[n] & 0x0f;
		*i = n + 1;
		*state = 0;
	} else {
		c = buf[n] >> 4;
		*state = 1;
	}

	return c;
}

/*
 * demux_spu_decompress() - decompress an RLE bitmap
 *
 * Arguments:
 *	handle	- pointer to demux context
 *	spu	- pointer to spu item
 *
 * Returns:
 *	pointer to decompressed spu image
 *	NULL if an error occurred
 */
char*
demux_spu_decompress(demux_handle_t *handle, spu_item_t *spu)
{
	int i, state;
	int x, y;
	unsigned int c;
	char *img;

	if ((img=malloc(spu->w*spu->h)) == NULL)
		return NULL;
	memset(img, 0, spu->w*spu->h);

	i = spu->line[0];
	x = y = 0;
	state = 0;
	PRINTF("spu: i %d size %d h %d\n", i, spu->size, spu->h);
	while ((i < spu->size) && (y < spu->h)) {
		unsigned int j;

		c = get_nibble(spu->data, &i, &state);
		
		if (c < 0x4) {
			c = (c << 4) |
				get_nibble(spu->data, &i, &state);
			if (c < 0x10) {
				c = (c << 4) |
					get_nibble(spu->data, &i, &state);
				if (c < 0x40) {
					c = (c << 4) |
						get_nibble(spu->data,
							   &i, &state);
					if (c <= 0x003) {
						if (state)
							i++;
						state = 0;
						
						while (x < spu->w) {
							img[(y*spu->w)+x] = c;
							x++;
						}
						y += 2;
						x = 0;
						if ((y >= spu->h) &&
						    !(y & 0x1)) {
							y = 1;
							i = spu->line[1];
						}
						continue;
					}
				}
			}
		}

		j = c >> 2;
		c &= 0x3;

		while (j > 0) {
			img[(y*spu->w)+x] = c;
			if (++x >= spu->w) {
				if (state)
					i++;
				state = 0;
				x = 0;
				y += 2;
			}
			j--;
		}

	}

#if 0
	{
		int a, b;
		int l, k;

		l = (spu->w >= 70) ? 70 : spu->w;
		k = (spu->h >= 480) ? 480 : spu->h;

		printf("l %d  k %d\n", l, k);

		for (a=0; a<k; a++) {
			for (b=0; b<l; b++) {
				printf("%x", img[(a*spu->w)+b]);
			}
			printf("\n");
		}
	}
#endif

	return img;
}

int
demux_set_display_size(demux_handle_t *handle, int w, int h)
{
	if (handle == NULL)
		return -1;

	handle->width = w;
	handle->height = h;

	return 0;
}

int
demux_set_audio_stream(demux_handle_t *handle, unsigned int id)
{
	int i;

	if (handle == NULL)
		return -1;

	if (handle->attr.audio.current == id)
		return 0;

	for (i=0; i<handle->attr.audio.existing; i++) {
		if (handle->attr.audio.ids[i].id == id) {
			handle->attr.audio.current = id;
			return handle->attr.audio.ids[i].type;
		}
	}

	return -1;
}

int
demux_set_video_stream(demux_handle_t *handle, unsigned int id)
{
	int i;

	if (handle == NULL)
		return -1;

	if (handle->attr.video.current == id)
		return 0;

	for (i=0; i<handle->attr.video.existing; i++) {
		if (handle->attr.video.ids[i].id == id) {
			handle->attr.video.current = id;
			return handle->attr.video.ids[i].type;
		}
	}

	return -1;
}

int demux_get_iframe(demux_handle_t *handle)
{
    return handle->allow_iframe;
}

void demux_set_iframe(demux_handle_t *handle,int type)
{
    handle->allow_iframe = type;
}
