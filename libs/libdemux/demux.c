/*
 *  Copyright (C) 2004, Jon Gettler
 *  http://mvpmc.sourceforge.net/
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

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mvp_demux.h"
#include "demux.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
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

	return handle;
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
demux_put(demux_handle_t *handle, char *buf, int len)
{
	int n, ret = 0;

	if (handle == NULL)
		return -1;

	if (buf == NULL)
		return -1;

 loop:
	if (len == 0)
		return 0;

	switch (handle->state) {
	case 0:
		/* start of stream */
		ret += start_stream(handle, buf, len);
		break;
	case 1 ... 4:
		/* frame header */
		ret += add_buffer(handle, buf, len);
		break;
	case MPEG_program_end_code:
	case pack_start_code:
	case system_header_start_code:
	case video_stream:
	case audio_stream:
	case private_stream_2:
	case private_stream_1:
	case padding_stream:
		/* middle of frame */
		n = parse_frame(handle, buf, len, handle->state);
		ret += n;
		buf += n;
		len -= n;
		if (handle->state == 1)
			goto loop;
		break;
	default:
		ret = -1;
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
demux_get_audio(demux_handle_t *handle, char *buf, int max)
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
demux_get_video(demux_handle_t *handle, char *buf, int max)
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

	return stream_drain_fd(handle->audio, fd);
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

	return stream_drain_fd(handle->video, fd);
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
		handle->video->tail = handle->video->size - 1;
		handle->video->attr->stats.cur_bytes = 0;
	}

	if (handle->audio) {
		handle->audio->buf = handle->video->buf + handle->video->size;
		handle->audio->head = 0;
		handle->audio->tail = handle->audio->size - 1;
		handle->audio->attr->stats.cur_bytes = 0;
	}

	handle->seeking = 1;

	return 0;
}

int
demux_reset(demux_handle_t *handle)
{
	if (handle->video) {
		handle->attr.video.bufsz = handle->size / 2;
		handle->video->size = handle->size / 2;

		handle->video->head = 0;
		handle->video->tail = handle->video->size - 1;
		handle->video->attr->stats.cur_bytes = 0;
	}

	if (handle->audio) {
		handle->attr.audio.bufsz = handle->size / 2;
		handle->audio->size = handle->size / 2;

		handle->audio->buf = handle->video->buf + handle->video->size;
		handle->audio->head = 0;
		handle->audio->tail = handle->audio->size - 1;
		handle->audio->attr->stats.cur_bytes = 0;
	}

	/*
	 * XXX: reset stats...
	 */

	handle->seeking = 1;

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
	handle->attr.gop_valid = 0;
}

