/*
 *  Copyright (C) 2004, Jon Gettler, Stephen Rice
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
 * This demuxer works as follows:
 *
 *    The incoming data stream is parsed based on mpeg frames.  Audio and
 *    video frames are saved, while other frames are ignored, since the
 *    hardware decoders don't need them.
 *
 *    The audio and video frames are stored in separate buffers, described
 *    by the stream_t data structure.  These buffers are allocated by a
 *    single call to malloc(), which allows the demuxer to resize the
 *    buffers easily.
 *
 *    The mpeg frames are parsed with a rather complicated finite state
 *    machine.  The complexity stems from the desire to allow data to be
 *    be added to the demuxer in any size, and at any rate.  This allows
 *    the library API to be very simple.
 *
 *    Testing has shown that this demuxer can feed the MediaMVP hardware
 *    decoders with a 12mbps VBR mpeg2 stream, when using 4MB of buffer
 *    space.
 *
 * Future work:
 *
 *    - Ability to flush a stream.
 *    - Support for switching between multiple audio and video streams
 *      within a single mpeg.
 *    - Make the buffer resizer more flexible.
 *    - More statistics.
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "mvp_demux.h"
#include "demux.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

/*
 * stream_resize() - Resize a stream buffer
 *
 * Arguments:
 *	stream	- stream pointer
 *	start	- new starting point of the stream buffer
 *	size	- size of new buffer
 *
 * Returns:
 *	0 if the resize succeeded, -1 if it failed
 */
int
stream_resize(stream_t *stream, char *start, int size)
{
	int bytes;

	PRINTF("stream buf 0x%p size %d head %d tail %d\n",
	       stream->buf, stream->size, stream->head, stream->tail);

	if (size == stream->size)
		return -1;

	if (size > stream->size)
		goto grow;

	if (stream->head >= stream->tail) {
		bytes = stream->head - stream->tail - 1;
	} else {
		bytes = stream->size - stream->tail - 1;
		bytes += stream->head;
	}

	if (size < (bytes + 16))
		return -1;

	if (stream->head >= stream->tail) {
		memmove(start, stream->buf+stream->tail, size);
		stream->tail = size;
		stream->head -= (stream->size - size);
		stream->size = size;
		stream->buf = start;
		PRINTF("stream buf 0x%p head %d tail %d size %d\n",
		       stream->buf, stream->head, stream->tail, stream->size);

		return 0;
	} else {
		memmove(start, stream->buf, stream->head);
		stream->tail -= (stream->size - size);
		stream->size = size;
		stream->buf = start;
		PRINTF("stream buf 0x%p head %d tail %d size %d\n",
		       stream->buf, stream->head, stream->tail, stream->size);

		return 0;
	}

	return -1;

 grow:
	if (stream->buf != (unsigned char*)start)
		return -1;

	if (stream->head >= stream->tail) {
		stream->size = size;
		PRINTF("stream buf 0x%p head %d tail %d size %d\n",
		       stream->buf, stream->head, stream->tail, stream->size);

		return 0;
	} else {
		memmove(stream->buf+stream->tail+(size-stream->size),
			stream->buf+stream->tail,
			stream->size - stream->tail);
		stream->tail += (size-stream->size);
		stream->size = size;
		PRINTF("stream buf 0x%p head %d tail %d size %d\n",
		       stream->buf, stream->head, stream->tail, stream->size);

		return 0;
	}

	return -1;
}

/*
 * stream_add() - Add a buffer to a media stream
 *
 * Arguments:
 *	stream	- stream pointer
 *	buf	- buffer containing media stream data
 *	len	- amount of data in the buffer
 *
 * Returns:
 *	number of bytes consumed
 */
static int
stream_add(stream_t *stream, char *buf, int len)
{
	unsigned int end;
	int size1, size2;

	if (len <= 0)
		return 0;

	PRINTF("%s(): stream 0x%p buf 0x%p len %d\n",
	       __FUNCTION__, stream, buf, len);
	PRINTF("stream size %d head %d tail %d\n", stream->size,
	       stream->head, stream->tail);

	if (len > stream->size)
		goto full;
	if (stream->head == stream->tail)
		goto full;

	end = (stream->head + len + 6) % stream->size;

	if (stream->head > stream->tail) {
		if ((end < stream->head) && (end > stream->tail))
			goto full;
	} else {
		if ((end > stream->tail) || (end < stream->head))
			goto full;
	}

	end = (stream->head + len) % stream->size;

	if (end > stream->head) {
		size1 = len;
		size2 = 0;
	} else {
		size1 = stream->size - stream->head;
		size2 = end;
	}

	if ((size1 + size2) < 1)
		goto full;

	PRINTF("size1 %d size2 %d\n", size1, size2);

	memcpy(stream->buf+stream->head, buf, size1);
	if (size2)
		memcpy(stream->buf, buf+size1, size2);

	stream->head = end;

	PRINTF("stream size %d head %d tail %d\n", stream->size,
	       stream->head, stream->tail);

	stream->attr->stats.bytes += (size1 + size2);
	stream->attr->stats.fill_count++;

	return (size1 + size2);

 full:
	PRINTF("%s(): stream full\n", __FUNCTION__);

	stream->attr->stats.full_count++;

	return 0;
}

/*
 * stream_drain() - Drain data out of a media stream buffer
 *
 * Arguments:
 *	stream	- stream pointer
 *	buf	- buffer to write stream data into
 *	max	- size of buffer
 *
 * Returns:
 *	number of bytes written into buffer
 */
int
stream_drain(stream_t *stream, char *buf, int max)
{
	int size1, size2;

	if (max <= 0)
		return 0;

	PRINTF("%s(): stream 0x%p buf 0x%p max %d\n",
	       __FUNCTION__, stream, buf, max);
	PRINTF("stream size %d head %d tail %d\n", stream->size,
	       stream->head, stream->tail);

	if (stream->head >= stream->tail) {
		size1 = stream->head - stream->tail - 1;
		size2 = 0;
	} else {
		size1 = stream->size - stream->tail - 1;
		size2 = stream->head;
	}

	if ((size1 + size2) > max) {
		if (size1 > max) {
			size1 = max;
			size2 = 0;
		} else {
			size2 = max - size1;
		}
	}

	PRINTF("size1 %d size2 %d\n", size1, size2);

	if (size1 > 0)
		memcpy(buf, stream->buf+stream->tail+1, size1);
	if (size2 > 0)
		memcpy(buf+size1, stream->buf, size2);

	stream->tail = (stream->tail + size1 + size2) % stream->size;

	PRINTF("stream size %d head %d tail %d\n", stream->size,
	       stream->head, stream->tail);

	if ((size1 + size2) == 0)
		stream->attr->stats.empty_count++;

	return size1 + size2;
}

/*
 * stream_drain_fd() - Drain data out of a media stream buffer to a file
 *                     descriptor
 *
 * Arguments:
 *	stream	- stream pointer
 *	fd	- file descriptor
 *
 * Returns:
 *	number of bytes written to the file descriptor
 */
int
stream_drain_fd(stream_t *stream, int fd)
{
	int size1, size2;
	int n, ret;

	PRINTF("%s(): stream 0x%p fd %d\n", __FUNCTION__, stream, fd);
	PRINTF("stream size %d head %d tail %d\n", stream->size,
	       stream->head, stream->tail);

	if (stream->head >= stream->tail) {
		size1 = stream->head - stream->tail - 1;
		size2 = 0;
	} else {
		size1 = stream->size - stream->tail - 1;
		size2 = stream->head;
	}

	PRINTF("size1 %d size2 %d\n", size1, size2);

	ret = 0;
	if (size1) {
		n = write(fd, stream->buf+stream->tail+1, size1);
		ret += n;
		if (n != size1) {
			size1 = n;
			size2 = 0;
			goto out;
		}
	}

	if (size2) {
		n = write(fd, stream->buf, size2);
		size2 = n;
		ret += n;
	}

 out:
	stream->tail = (stream->tail + size1 + size2) % stream->size;

	PRINTF("%s(): wrote %d bytes, head %d tail %d\n", __FUNCTION__,
	       ret, stream->head, stream->tail);

	if ((size1 + size2) == 0)
		stream->attr->stats.empty_count++;
	else
		stream->attr->stats.drain_count++;

	return (size1 + size2);
}

/*
 * parse_frame() - Parse a single frame in the media stream
 *
 * Arguments:
 *	handle	- demux context handle
 *	buf	- buffer containing media stream data
 *	len	- length of buffer
 *	type	- type of buffer (0 if not yet known)
 *
 * Returns:
 *	number of bytes consumed from the buffer
 */
int
parse_frame(demux_handle_t *handle, char *buf, int len, int type)
{
	int n, m, ret = 0;
	unsigned char header[4];
	int i;

	if (len <= 0)
		return 0;

	if (type == 0) {
		type = buf[0];
		ret++;
		buf++;
		handle->frame_state = 0;
		handle->remain = 0;
		handle->state = type;
		handle->bufsz = 0;
		memset(handle->buf, 0, sizeof(handle->buf));
		handle->headernum = 0;
	}

	if (handle->remain) {
		if (handle->remain <= (len-ret))
			n = handle->remain;
		else
			n = (len-ret);

		switch (handle->frame_state) {
		case 0:
			/* misc data */
			break;
		case 1:
			/* audio */
			m = stream_add(handle->audio, buf, n);
			n = m;
			break;
		case 2:
			/* video */
			m = stream_add(handle->video, buf, n);
			n = m;
			break;
		case 3:
			/* mpeg2 pack code */
			goto mpeg2;
			break;
		default:
			/* reset to start of frame */
			handle->state = 1;
			break;
		}

		if (handle->remain == n)
			handle->state = 1;
		else
			handle->remain -= n;

		buf += n;
		ret += n;

		return ret;
	}

	switch (type) {
	case MPEG_program_end_code:
		/* reset to start of frame */
		handle->state = 1;
		PRINTF("found program end\n");
		break;
	case pack_start_code:
		PRINTF("found pack frame\n");

		if (((len-ret) + handle->bufsz) >= 7) {
			n = 7 - handle->bufsz;
			memcpy(handle->buf+handle->bufsz, buf, n);
			buf += n;
			ret += n;
			handle->bufsz += n;
		} else {
			n = len - ret;
			memcpy(handle->buf+handle->bufsz, buf, n);
			buf += n;
			ret += n;
			handle->bufsz += n;
			break;
		}

		if ((handle->buf[0] & 0xf0) == 0x20) {
			/* mpeg1 */
			PRINTF("mpeg1\n");
			handle->attr.video.type = 1;
			handle->state = 1;
		} else if ((handle->buf[0] & 0xc0) == 0x40) {
			/* mpeg2 */
			PRINTF("mpeg2\n");
			handle->attr.video.type = 2;
		mpeg2:
			if ((len-ret) >= 3) {
				n = buf[2] & 0x7;
				if ((len-ret) >= (2+n)) {
					buf += 2 + n;
					ret += 2 + n;
					handle->state = 1;
				} else {
					handle->remain =
						(2+n) - (len-ret);
					buf += (len-ret);
					ret += (len-ret);
					handle->frame_state = 1;
				}
			} else {
				n = len - ret;
				memcpy(handle->buf, buf, n);
				buf += n;
				ret += n;
				handle->bufsz = n;
				handle->frame_state = 3;
				handle->remain = 3 - n;
			}
		} else {
			/* reset to start of frame */
			handle->state = 1;
		}
		break;
	case system_header_start_code:
	case private_stream_2:
	case private_stream_1:
	case padding_stream:
		PRINTF("found other stream, type %d\n", type);

		if (((len-ret) + handle->bufsz) >= 2) {
			n = 2 - handle->bufsz;
			memcpy(handle->buf+handle->bufsz, buf, n);
			buf += n;
			ret += n;
			handle->bufsz += n;
		} else {
			n = len - ret;
			memcpy(handle->buf+handle->bufsz, buf, n);
			buf += n;
			ret += n;
			handle->bufsz += n;
			break;
		}

		n = handle->buf[0] * 256 + handle->buf[1];
		if (n <= (len-ret)) {
			buf += n;
			ret += n;
			handle->state = 1;
		} else {
			handle->remain = n - (len-ret);
			handle->frame_state = 1;
			buf += (len-ret);
			ret += (len-ret);
		}
		break;
	case video_stream:
		PRINTF("found video stream\n");

		if (((len-ret) + handle->bufsz) >= 2) {
			n = 2 - handle->bufsz;
			memcpy(handle->buf+handle->bufsz, buf, n);
			buf += n;
			ret += n;
			handle->bufsz += n;
		} else {
			n = len - ret;
			memcpy(handle->buf+handle->bufsz, buf, n);
			buf += n;
			ret += n;
			handle->bufsz += n;
			break;
		}

		if (handle->headernum == 0) {
			header[0] = 0;
			header[1] = 0;
			header[2] = 1;
			header[3] = video_stream;
			if (stream_add(handle->video, header, 4) != 4) {
				break;
			}
			handle->headernum++;
		}

		if (handle->headernum == 1) {
			if (stream_add(handle->video, handle->buf,
				       handle->bufsz) != handle->bufsz) {
				break;
			}
			handle->headernum++;
		}

		n = handle->buf[0] * 256 + handle->buf[1];

		PRINTF("video frame size %d\n", n);
		handle->attr.video.stats.frames++;

		if (n <= (len-ret)) {
			m = stream_add(handle->video, buf, n);
			PRINTF("line %d: n %d m %d\n", __LINE__, n, m);
			buf += m;
			ret += m;
			if (n == m) {
				handle->state = 1;
			} else {
				handle->remain = n - m;
				handle->frame_state = 2;
			}
		} else {
			m = stream_add(handle->video, buf, (len-ret));
			PRINTF("line %d: n %d m %d\n", __LINE__, n, m);
			handle->remain = n - m;
			handle->frame_state = 2;
			buf += m;
			ret += m;
		}
		break;
	case audio_stream:
		PRINTF("found audio stream\n");

		if (((len-ret) + handle->bufsz) >= 2) {
			n = 2 - handle->bufsz;
			memcpy(handle->buf+handle->bufsz, buf, n);
			buf += n;
			ret += n;
			handle->bufsz += n;
		} else {
			n = len - ret;
			memcpy(handle->buf+handle->bufsz, buf, n);
			buf += n;
			ret += n;
			handle->bufsz += n;
			break;
		}

		if (handle->headernum == 0) {
			header[0] = 0;
			header[1] = 0;
			header[2] = 1;
			header[3] = audio_stream;
			if (stream_add(handle->audio, header, 4) != 4) {
				break;
			}
			handle->headernum++;
		}

		if (handle->headernum == 1) {
			if (stream_add(handle->audio, handle->buf,
				       handle->bufsz) != handle->bufsz) {
				break;
			}
			handle->headernum++;
		}

		n = handle->buf[0] * 256 + handle->buf[1];
		PRINTF("audio frame size %d\n", n);
		handle->attr.audio.stats.frames++;

		if ((len-ret) >= 20) {
			for (i=0; i<14; i++)
				if (buf[i] != 0xff) {
					PRINTF("audio type 0x%.2x\n",
					       buf[i] & 0xc0);
					switch (buf[i] & 0xc0) {
					case 0x80:
						handle->attr.audio.type =
							AUDIO_MODE_MPEG2_PES;
						break;
					case 0x40:
						handle->attr.audio.type =
							AUDIO_MODE_MPEG1_PES;
						break;
					}
					break;
				}
		}

		if (n <= (len-ret)) {
			m = stream_add(handle->audio, buf, n);
			buf += m;
			ret += m;
			if (n == m) {
				handle->state = 1;
			} else {
				handle->remain = n - m;
				handle->frame_state = 1;
			}
		} else {
			m = stream_add(handle->audio, buf, (len-ret));
			handle->remain = n - m;
			handle->frame_state = 1;
			buf += m;
			ret += m;
		}
		break;
	default:
		/* reset to start of frame */
		handle->state = 1;
		PRINTF("unknown state 0x%.2x\n", type);
		break;
	}

	PRINTF("%s(): consumed %d bytes\n", __FUNCTION__, ret);

	return ret;
}

/*
 * add_buffer() - Process a buffer containing media stream data
 *
 * Arguments:
 *	handle	- demux context handle
 *	buf	- buffer containing media stream data
 *	len	- length of buffer
 *
 * Returns:
 *	number of bytes consumed from the buffer
 */
int
add_buffer(demux_handle_t *handle, char *buf, int len)
{
	int n, ret = 0;

	while ((len-ret) > 0) {
		switch (handle->state) {
		case 1:
			if (buf[0] == 0)
				handle->state = 2;
			break;
		case 2:
			if (buf[0] == 0)
				handle->state = 3;
			else
				handle->state = 1;
			break;
		case 3:
			if (buf[0] == 1)
				handle->state = 4;
			else if (buf[0] != 0)
				handle->state = 1;
			break;
		case 4:
			PRINTF("calling parse_frame() at offset %d\n", ret);
			n = parse_frame(handle, buf, len-ret, 0);
			ret += n - 1;
			buf += n - 1;
			PRINTF("parse_frame() returned at offset %d\n", ret);
			if (handle->state != 1) {
				PRINTF("breaking out of state 4\n");
				ret++;
				buf++;
				goto out;
			}
			break;
		default:
			/* reset to start of frame */
			handle->state = 1;
			break;
		}
		ret++;
		buf++;
	}

 out:
	PRINTF("%s(): processed %d bytes\n", __FUNCTION__, ret);

	return ret;
}

/*
 * stream_init() - Create and initialize a media stream
 *
 * Arguments:
 *	buf	- buffer containing media stream data
 *	size	- size of buffer
 *
 * Returns:
 *	pointer to stream data
 */
static stream_t*
stream_init(char *buf, int size)
{
	stream_t *stream = NULL;

	if ((stream=malloc(sizeof(*stream))) == NULL)
		goto err;
	stream->buf = buf;
	memset(stream->buf, 0, size);
	stream->size = size;
	stream->head = 0;
	stream->tail = size - 1;

	PRINTF("%s(): stream 0x%p size %d\n", __FUNCTION__, stream, size);

	return stream;

 err:
	if (stream)
		free(stream);

	return NULL;
}

/*
 * start_stream() - Start processing a media stream
 *
 * Arguments:
 *	handle	- demux context handle
 *	buf	- buffer containing media stream data
 *	len	- length of buffer
 *
 * Returns:
 *	number of bytes consumed from the buffer
 */
int
start_stream(demux_handle_t *handle, char *buf, int len)
{
	int ret = 0, n;
	char *stream_buf;

	if (len < 4)
		return 0;

	if ((stream_buf=malloc(handle->size)) == NULL)
		return -1;

	n = handle->size / 2;
	if ((handle->video=stream_init(stream_buf, n)) == NULL)
		goto err;
	if ((handle->audio=stream_init(stream_buf+n, n)) == NULL)
		goto err;

	handle->audio->attr = &handle->attr.audio;
	handle->video->attr = &handle->attr.video;

	handle->state = 1;

	ret += add_buffer(handle, buf+ret, len-ret);

	return ret;

 err:
	if (stream_buf)
		free(stream_buf);

	return -1;
}
