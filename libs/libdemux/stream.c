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
#include "mvp_av.h"
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
		stream->head -= stream->tail;
		stream->tail = size;
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
 *	handle	- demux context handle
 *	stream	- stream pointer
 *	buf	- buffer containing media stream data
 *	len	- amount of data in the buffer
 *
 * Returns:
 *	number of bytes consumed
 */
static int
stream_add(demux_handle_t *handle, stream_t *stream, char *buf, int len)
{
	unsigned int end, tail;
	int size1, size2;

	if (len <= 0)
		return 0;

	if (handle->seeking)
		return len;

	tail = stream->tail;

	PRINTF("%s(): stream 0x%p buf 0x%p len %d\n",
	       __FUNCTION__, stream, buf, len);
	PRINTF("stream size %d head %d tail %d\n", stream->size,
	       stream->head, tail);

	if (len > stream->size)
		goto full;
	if (stream->head == tail)
		goto full;

	end = (stream->head + len + 6) % stream->size;

	if (stream->head > tail) {
		if ((end < stream->head) && (end > tail))
			goto full;
	} else {
		if ((end > tail) || (end < stream->head))
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
	       stream->head, tail);

	stream->attr->stats.cur_bytes += (size1 + size2);
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
	unsigned int head;

	if (max <= 0)
		return 0;

	head = stream->head;

	PRINTF("%s(): stream 0x%p buf 0x%p max %d\n",
	       __FUNCTION__, stream, buf, max);
	PRINTF("stream size %d head %d tail %d\n", stream->size,
	       head, stream->tail);

	if (head >= stream->tail) {
		size1 = head - stream->tail - 1;
		size2 = 0;
	} else {
		size1 = stream->size - stream->tail - 1;
		size2 = head;
	}

	PRINTF("size1 %d size2 %d\n", size1, size2);

	if ((size1 + size2) == 0)
		goto empty;

	if ((size1 + size2) > max) {
		if (size1 > max) {
			size1 = max;
			size2 = 0;
		} else {
			size2 = max - size1;
		}
	}

	if (size1 > 0)
		memcpy(buf, stream->buf+stream->tail+1, size1);
	if (size2 > 0)
		memcpy(buf+size1, stream->buf, size2);

	stream->tail = (stream->tail + size1 + size2) % stream->size;

	PRINTF("stream size %d head %d tail %d\n", stream->size,
	       head, stream->tail);

	stream->attr->stats.cur_bytes -= (size1 + size2);

	return size1 + size2;

 empty:
	stream->attr->stats.empty_count++;

	return 0;
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
	unsigned int head;
	int size1, size2;
	int n, ret;

	head = stream->head;

	PRINTF("%s(): stream 0x%p fd %d\n", __FUNCTION__, stream, fd);
	PRINTF("stream size %d head %d tail %d\n", stream->size,
	       head, stream->tail);

	if (head >= stream->tail) {
		size1 = head - stream->tail - 1;
		size2 = 0;
	} else {
		size1 = stream->size - stream->tail - 1;
		size2 = head;
	}

	PRINTF("%s(): size1 %d size2 %d\n", __FUNCTION__, size1, size2);

	ret = 0;
	if (size1) {
		n = write(fd, stream->buf+stream->tail+1, size1);
		if (n < 0)
			return 0;
		ret += n;
		if (n != size1) {
			size1 = n;
			size2 = 0;
			goto out;
		}
	}

	if (size2) {
		n = write(fd, stream->buf, size2);
		if (n < 0) {
			size2 = 0;
		} else {
			size2 = n;
			ret += n;
		}
	}

 out:
	stream->tail = (stream->tail + size1 + size2) % stream->size;

	PRINTF("%s(): wrote %d bytes, head %d tail %d\n", __FUNCTION__,
	       ret, head, stream->tail);

	if ((size1 + size2) == 0) {
		stream->attr->stats.empty_count++;
	} else {
		stream->attr->stats.drain_count++;
		stream->attr->stats.cur_bytes -= (size1 + size2);
	}

	return (size1 + size2);
}

/*
 * parse_video_frame() - Parse a video frame header
 *
 * Arguments:
 *	handle	- demux context handle
 *	buf	- buffer containing media stream data
 *	len	- length of buffer
 *
 * Returns:
 *	0	if a sync point was found (sequence header, GOP, I-frame)
 *	-1	if no sync point was found
 */
static inline int
parse_video_frame(demux_handle_t *handle, unsigned char *buf, int len)
{
	int i;
	int h, w;
	int aspect, frame_rate;
	int type;
	int hour, minute, second, frame;
	int ret = -1;
	video_info_t *vi;
	unsigned int dts, pts;
	int delta;

	dts = (buf[1] >> 6) & 0x1;
	pts = (buf[1] >> 7);

	if (pts) {
		pts = (buf[7] >> 1) |
			(buf[6] << 7) |
			((buf[5] >> 1) << 15) |
			(buf[4] << 22) |
			(((buf[3] >> 1) & 0x7) << 30);
		PRINTF("pts 0x%.8x\n", pts);
	}
	if (dts) {
		dts = (buf[8] >> 1) |
			(buf[9] << 7) |
			((buf[10] >> 1) << 15) |
			(buf[11] << 22) |
			(((buf[12] >> 1) & 0x7) << 30);
		PRINTF("dts 0x%.8x\n", dts);
	}

	for (i=2; i<(len-4); i++)
		if ((buf[i] == 0) && (buf[i+1] == 0) &&
		    (buf[i+2] == 1)) {
			switch (buf[i+3]) {
			case 0x0:
				type = (buf[i+5] >> 3) & 7;
				switch (type) {
				case 1:
					PRINTF("picture I frame\n");
					break;
				case 2:
					PRINTF("picture P frame\n");
					break;
				case 3:
					PRINTF("picture B frame\n");
					break;
				case 4:
					PRINTF("picture D frame\n");
					break;
				default:
					PRINTF("picture 0x%x\n", type);
					break;
				}
				if (type == 1)
					ret = 0;
				goto out;
				break;
			case 0xb3:
				w = (buf[i+5] >> 4) |
					(buf[i+4] << 4);
				h = ((buf[i+5] & 0xf) << 8) |
					buf[i+6];
				/*
				 * allow PAL to display under NTSC by
				 * changing the video height
				 */
				if ((handle->height == 480) &&
				    (h == 576)) {
					buf[i+6] = 480 & 0xff;
					buf[i+5] = ((480 >> 8) & 0xf) |
						(buf[i+5] & 0xf0);
				}
				aspect = buf[i+7] >> 4;
				frame_rate = buf[i+7] & 0xf;
				PRINTF("SEQ: %dx%d, aspect %d fr %d\n",
				       w, h, aspect, frame_rate);
				vi = &handle->attr.video.stats.info.video;
				vi->width = w;
				vi->height = h;
				vi->aspect = aspect;
				vi->frame_rate = frame_rate;
				ret = 0;
				break;
			case 0xb8:
				if ((buf[i+7] & 0x1f) != 0)
					break;
				hour = (buf[i+4] >> 2) & 0x1f;
				minute = (buf[i+5] >> 4) |
					((buf[i+4] & 0x3) << 4);
				second = ((buf[i+5] & 0x7) << 3) |
					(buf[i+6] >> 5);
				frame = ((buf[i+6] & 0x1f) << 1) |
					(buf[i+7] >> 7);
				delta = (hour - handle->attr.gop.hour) * 3600 +
					(minute - handle->attr.gop.minute) * 60 +
					(second - handle->attr.gop.second);

				PRINTF("GOP: %.2d:%.2d:%.2d %d [%d] PTS 0x%.8x %d\n",
				       hour, minute, second, frame, i, pts, handle->bytes);
				if (handle->seeking == 0) {
					int newbps = 0;

					/* BPS from pts if possible */
					if (handle->attr.gop.pts &&
					    pts > handle->attr.gop.pts) {
						/* Calculate BPS from PTS difference. The PTS/1000 expression avoids integer overflow */
						newbps = ((handle->bytes - handle->attr.gop.offset)*(PTS_HZ/1000)/(pts-handle->attr.gop.pts))*1000;
					} else { /* Fall back on GOP timestamp */
						delta = (hour - handle->attr.gop.hour) * 3600 +
							(minute - handle->attr.gop.minute) * 60 +
							(second - handle->attr.gop.second);
						if (delta > 0)
							newbps = (handle->bytes - handle->attr.gop.offset)/delta;
					}
					if (newbps != 0) {
						handle->attr.bps = newbps;
						PRINTF("BPS: %d\n",
						       handle->attr.bps);
						handle->attr.gop.offset = handle->bytes;
						handle->attr.gop.hour = hour;
						handle->attr.gop.minute = minute;
						handle->attr.gop.second = second;
						handle->attr.gop.frame = frame;
						handle->attr.gop.pts = pts;
						handle->attr.gop_valid = 1;
					}
					ret = 0;
					goto out;
				} else {
					handle->attr.bps = 0;
					handle->attr.gop.offset = handle->bytes;
					handle->attr.gop.hour = hour;
					handle->attr.gop.minute = minute;
					handle->attr.gop.second = second;
					handle->attr.gop.frame = frame;
					handle->attr.gop.pts = pts;
					handle->attr.gop_valid = 1;
				}
				ret = 0;
				goto out;
				break;
			}
			i += 4;
		}

 out:
	return ret;
}

/*
 * parse_spu_frame() - Parse a subpicture frame
 *
 * Arguments:
 *	handle	- demux context handle
 *	buf	- buffer containing an entire private stream frame
 *	len	- length of buffer
 *
 * Returns:
 *	0	if a subpicture was found and parsed
 *	-1	if an error occurred, or the frame was not a subpicture
 */
static int
parse_spu_frame(demux_handle_t *handle, unsigned char *buf, int len)
{
	unsigned int pts[2], color = 0, alpha = 0;
	unsigned int x = 0, y = 0, w=0, h=0, line1 = 0, line2 = 0;
	unsigned int id, start, cmd;
	unsigned int size, data_size = 0;
	int i, end, header;

	id = buf[buf[2] + 3] - 32;
	header = buf[2] + 3 + 1;

	if (id > 31)
		return -1;

	if (len <= 0)
		return -1;

	start = (buf[7] >> 1) | (buf[6] << 7) | ((buf[5] >> 1) << 14) |
		(buf[4] << 22) | (((buf[3] >> 1) & 0x7) << 29);

	if (handle->spu[id].size == 0) {
		/* complete subpicture or first frame */
		buf += header;
		size = (buf[0] * 256) + buf[1];
		data_size = (buf[2] * 256) + buf[3];
		cmd = data_size + 4;

		PRINTF("start 0x%.8x\n", start);
		if (size <= len) {
			/* complete subpicture */
			PRINTF("complete subpicture, stream %d\n", id);
			handle->spu[id].len = size;
		} else {
			/* first frame */
			PRINTF("first subpicture frame, stream %d\n", id);
			PRINTF("size %d data_size %d\n", size, data_size);
			handle->spu[id].size = size;
			handle->spu[id].data_size = data_size;
			handle->spu[id].len = len - header;
			handle->spu[id].buf = malloc(size);
			PRINTF("malloc 1: 0x%.8x\n", handle->spu[id].buf);
			memset(handle->spu[id].buf, 0, size);
			memcpy(handle->spu[id].buf, buf, len-header);
			PRINTF("memcpy: off %d len %d\n", 0,
			       len-header);

			return 0;
		}
	} else {
		/* secondary or final frame */
		if (handle->spu[id].size <
		    (handle->spu[id].len + len - header)) {
			fprintf(stderr,
				"libdemux: too much spu data! (%d < %d)\n",
				handle->spu[id].size,
				handle->spu[id].len + len - header);
			handle->spu[id].size = 0;
			return -1;
		}
		buf += header;
		memcpy(handle->spu[id].buf+handle->spu[id].len,
		       buf, len-header);
		PRINTF("memcpy: off %d len %d\n", handle->spu[id].len,
		       len-header);
		handle->spu[id].len += len - header;

		if (handle->spu[id].len != handle->spu[id].size) {
			PRINTF("secondary frame, stream %d, [%d %d]\n", id,
			       handle->spu[id].len, handle->spu[id].size);
			return 0;
		}

		PRINTF("final subpicture frame, stream %d\n", id);

		cmd = handle->spu[id].data_size + 4;
		buf = handle->spu[id].buf;
		size = handle->spu[id].size;
		data_size = handle->spu[id].data_size;

		PRINTF("size %d cmd %d\n", size, cmd);
	}

	handle->attr.spu[id].bytes += handle->spu[id].len;
	handle->attr.spu[id].frames++;
	handle->spu[id].len = 0;
	handle->spu[id].size = 0;

	PRINTF("subtitle stream %d, size %d %d\n", id, size, data_size);

	while (cmd < size) {
		PRINTF("command %d [%d %d]\n", buf[cmd], cmd, size);
		switch (buf[cmd]) {
		case 0x0:
			/* force display */
			pts[0] = start;
			PRINTF("pts 0x%.8x\n", pts[0]);
			cmd++;
			break;
		case 0x1:
			/* start display */
			pts[0] = start;
			PRINTF("start pts 0x%.8x\n", pts[0]);
			cmd++;
			break;
		case 0x2:
			/* end display */
			pts[1] = start;
			PRINTF("end pts 0x%.8x\n", pts[0]);
			cmd++;
			break;
		case 0x3:
			/* palette */
			color = (buf[cmd+1] * 256) |
				buf[cmd+2];
			cmd += 3;
			PRINTF("color 0x%.8x\n", color);
			break;
		case 0x4:
			/* alpha channel */
			alpha = (buf[cmd+1] * 256) |
				buf[cmd+2];
			cmd += 3;
			PRINTF("alpha 0x%.8x\n", alpha);
			break;
		case 0x5:
			/* coordinates */
			x = (((buf[cmd + 1]) << 4) +
			     (buf[cmd + 2] >> 4));
			w = (((buf[cmd + 2] & 0x0f) << 8) +
			     buf[cmd + 3]) - x + 1;
			y = (((buf[cmd + 4]) << 4) +
			     (buf[cmd + 5] >> 4));
			h = (((buf[cmd + 5] & 0x0f) << 8) +
			     buf[cmd + 6]) - y + 1;
			cmd += 7;
			PRINTF("coord: %d %d  %d %d\n",
			       x, y, w, h);
			break;
		case 0x6:
			/* RLE offsets */
			line1 = ((buf[cmd + 1]) << 8) +
				buf[cmd + 2]; 
			line2 = ((buf[cmd + 3]) << 8) +
				buf[cmd + 4];
			cmd += 5;
			PRINTF("offsets: %d %d\n",
			       line1, line2);
			break;
		case 0xff:
			/* end */
			cmd = size;
			PRINTF("end command\n");
			break;
		default:
			fprintf(stderr, 
				"unknown spu command 0x%x\n", buf[cmd]);
			goto err;
			break;
		}
	}

	if ((w == 0) || (h == 0)) {
		fprintf(stderr, "error in subpicture\n");
		goto err;
	}
	
	PRINTF("image size %dx%d, %d\n", w, h, w*h);

	/*
	 * To avoid wasting too much memory, only maintain the 4 most recent
	 * subpictures per stream, unless the stream is currently being
	 * viewed.
	 */
	if (handle->spu[id].inuse)
		end = SPU_MAX-1;
	else
		end = 3;

	if (handle->spu[id].item[0].data) {
		PRINTF("free 2: 0x%.8x\n", handle->spu[id].item[0].data);
		free(handle->spu[id].item[0].data);
		handle->spu[id].item[0].data = NULL;
	}

	for (i=0; i<end && handle->spu[id].item[i+1].data; i++) {
		memcpy(handle->spu[id].item+i,
		       handle->spu[id].item+i+1,
		       sizeof(handle->spu[id].item[i]));
	}
	memset(handle->spu[id].item+i, 0, sizeof(handle->spu[id].item[i]));

	PRINTF("sizes: %d %d\n", handle->spu[id].size, data_size);
	handle->spu[id].item[i].data = malloc(data_size);
	PRINTF("malloc 2: 0x%.8x\n", handle->spu[id].item[i].data);
	memcpy(handle->spu[id].item[i].data, buf, data_size);

	handle->spu[id].item[i].size = data_size;
	handle->spu[id].item[i].x = x;
	handle->spu[id].item[i].y = y;
	handle->spu[id].item[i].w = w;
	handle->spu[id].item[i].h = h;
	handle->spu[id].item[i].color = color;
	handle->spu[id].item[i].alpha = alpha;
	handle->spu[id].item[i].start = pts[0];
	handle->spu[id].item[i].end = pts[1];
	handle->spu[id].item[i].line[0] = line1;
	handle->spu[id].item[i].line[1] = line2;

	PRINTF("spu stream %d item %d\n", id, i);

	if (handle->spu[id].buf) {
		PRINTF("free 1: 0x%.8x\n", handle->spu[id].buf);
		free(handle->spu[id].buf);
		handle->spu[id].buf = NULL;
	}

	PRINTF("done with subtitle\n");

	return 0;

 err:
	PRINTF("error in subtitle stream, size %d %d len %d\n",
	       size, data_size, len);

	return -1;
}

/*
 * parse_ac3_frame() - Parse an AC3 audio frame
 *
 * Arguments:
 *	handle	- demux context handle
 *	buf	- buffer containing an entire private stream frame
 *	len	- length of buffer
 *
 * Returns:
 *	>=0	number of bytes consumed
 *	-1	if an error occurred, or the frame was not AC3 audio
 */
static int
parse_ac3_frame(demux_handle_t *handle, unsigned char *buf, int len)
{
	unsigned int id;
	int m, header, offset;

	id = buf[buf[2] + 3] - 128;
	header = buf[2] + 3 + 1;

	if ((id < 0) || (id > 31))
		return -1;

	register_stream(handle->audio, id + 128, STREAM_AC3);
	handle->attr.audio.type = AUDIO_MODE_AC3;

	PRINTF("AC3 stream %d len %d size %d\n", id, len, size);

	offset = buf[2] + 7;

	if (handle->audio->attr->current == -1)
		handle->audio->attr->current = id + 128;

	if ((id + 128) == handle->audio->attr->current) {
		handle->attr.ac3_audio = 1;
		/*
		 * XXX: broken if the demuxer fills up
		 */
		m = stream_add(handle, handle->audio, buf+offset, len-offset);

		return m + offset;
	} else {
		return len;
	}

	return 0;
}

/*
 * parse_pcm_frame() - Parse an PCM audio frame
 *
 * Arguments:
 *	handle	- demux context handle
 *	buf	- buffer containing an entire private stream frame
 *	len	- length of buffer
 *
 * Returns:
 *	>=0	number of bytes consumed
 *	-1	if an error occurred, or the frame was not PCM audio
 */
static int
parse_pcm_frame(demux_handle_t *handle, unsigned char *buf, int len)
{
	unsigned int id;
	int m, header, offset;

	id = buf[buf[2] + 3] - 160;
	header = buf[2] + 3 + 1;

	if ((id < 0) || (id > 31))
		return -1;

	register_stream(handle->audio, id + 160, STREAM_PCM);
	handle->attr.audio.type = AUDIO_MODE_PCM;

	PRINTF("PCM stream %d len %d size %d\n", id, len, size);

	offset = buf[2] + 7;

	if (handle->audio->attr->current == -1)
		handle->audio->attr->current = id + 160;

	if ((id + 160) == handle->audio->attr->current) {
		/*
		 * XXX: broken if the demuxer fills up
		 */
		m = stream_add(handle, handle->audio, buf+offset, len-offset);

		return m + offset;
	} else {
		return len;
	}

	return 0;
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
parse_frame(demux_handle_t *handle, unsigned char *buf, int len, int type)
{
	int n, m, ret = 0;
	unsigned char header[4];
	int i;

	if (len <= 0)
		return 0;

	PRINTF("parse_frame(): len %d type %d remain %d\n", len, type,
	       handle->remain);

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
			m = stream_add(handle, handle->audio, buf, n);
			n = m;
			break;
		case 2:
			/* video */
			m = stream_add(handle, handle->video, buf, n);
			n = m;
			break;
		case 3:
			/* mpeg2 pack code */
			PRINTF("mpeg2 frame_state 3, remain %d len %d\n",
			       handle->remain, len);
			if (handle->remain < len) {
				n = handle->remain;
			} else {
				n = len;
			}
			break;
		case 4:
			/* private stream 1 */
			memcpy(handle->spu_buf+handle->spu_len, buf, n);
			handle->spu_len += n;

			if (handle->remain == n) {
				if (parse_spu_frame(handle,
						    handle->spu_buf,
						    handle->spu_len) == 0) {
					PRINTF("found partial sub picture\n");
				} else if (parse_ac3_frame(handle,
							   handle->spu_buf,
							   handle->spu_len) >= 0) {
					PRINTF("found AC3 frame\n");
				} else if (parse_pcm_frame(handle,
							   handle->spu_buf,
							   handle->spu_len) >= 0) {
					PRINTF("found PCM frame\n");
				}
				handle->spu_len = 0;
			}
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
					PRINTF("mpeg2 remain %d len %d\n",
					       handle->remain, len);
				}
			} else {
				n = len - ret;
				memcpy(handle->buf, buf, n);
				buf += n;
				ret += n;
				handle->bufsz = n;
				handle->frame_state = 3;
				handle->remain = 3 - n;
				PRINTF("mpeg2 (2) remain %d len %d\n",
				       handle->remain, len);
			}
		} else {
			/* reset to start of frame */
			handle->state = 1;
			PRINTF("unknown pack code\n");
		}
		break;
	case private_stream_1:
	case private_stream_2:
	case system_header_start_code:
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
			if (type == private_stream_1) {
				if (parse_spu_frame(handle, buf, n) == 0) {
					PRINTF("found sub picture\n");
				} else if (parse_ac3_frame(handle,
							   buf, n) >= 0) {
					PRINTF("found AC3 frame\n");
				} else if (parse_pcm_frame(handle,
							   buf, n) >= 0) {
					PRINTF("found PCM frame\n");
				}
			}
			buf += n;
			ret += n;
			handle->state = 1;
		} else {
			handle->remain = n - (len-ret);
			if (type == private_stream_1) {
				handle->frame_state = 4;
				memcpy(handle->spu_buf, buf, (len-ret));
				handle->spu_len = len-ret;
			} else {
				handle->frame_state = 0;
			}
			buf += (len-ret);
			ret += (len-ret);
		}
		break;
	case video_stream_0 ... video_stream_7:
		PRINTF("found video stream, stream %p\n", handle->video);

		register_stream(handle->video, type, STREAM_MPEG);

		if (handle->video->attr->current == -1)
			handle->video->attr->current = type;

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

#if 1
		if (type != handle->video->attr->current) {
			/*
			 * Throw stream away if it is not currently
			 * being played.
			 */
			n = handle->buf[0] * 256 + handle->buf[1];
			handle->remain = n;
			handle->frame_state = 0;
			break;
		}
#endif

		if (handle->headernum == 0) {
			header[0] = 0;
			header[1] = 0;
			header[2] = 1;
			header[3] = type;
			if (stream_add(handle, handle->video,
				       header, 4) != 4) {
				break;
			}
			handle->headernum++;
		}

		if (handle->headernum == 1) {
			if (stream_add(handle, handle->video, handle->buf,
				       handle->bufsz) != handle->bufsz) {
				break;
			}
			handle->headernum++;
		}

		n = handle->buf[0] * 256 + handle->buf[1];

		PRINTF("video frame size %d\n", n);
		handle->attr.video.stats.frames++;

		if (n <= (len-ret)) {
			if ((parse_video_frame(handle, buf, n) == 0) &&
			    (handle->seeking == 1)) {
				handle->seeking = 0;
				header[0] = 0;
				header[1] = 0;
				header[2] = 1;
				header[3] = type;
				stream_add(handle, handle->video, header, 4);
				stream_add(handle, handle->video, handle->buf,
					   handle->bufsz);
			}

			m = stream_add(handle, handle->video, buf, n);
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
			m = stream_add(handle, handle->video, buf, (len-ret));
			PRINTF("line %d: n %d m %d\n", __LINE__, n, m);
			handle->remain = n - m;
			handle->frame_state = 2;
			buf += m;
			ret += m;
		}
		break;
	case audio_stream_0 ... audio_stream_7:
		PRINTF("found audio stream\n");

		register_stream(handle->audio, type, STREAM_MPEG);

		if (handle->audio->attr->current == -1)
			handle->audio->attr->current = type;

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

#if 1
		if (type != handle->audio->attr->current) {
			/*
			 * Throw stream away if it is not currently
			 * being played.
			 */
			n = handle->buf[0] * 256 + handle->buf[1];
			handle->remain = n;
			handle->frame_state = 0;
			break;
		}
#endif

		if (handle->headernum == 0) {
			header[0] = 0;
			header[1] = 0;
			header[2] = 1;
			header[3] = type;
			if (stream_add(handle, handle->audio,
				       header, 4) != 4) {
				break;
			}
			handle->headernum++;
		}

		if (handle->headernum == 1) {
			if (stream_add(handle, handle->audio, handle->buf,
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
			m = stream_add(handle, handle->audio, buf, n);
			buf += m;
			ret += m;
			if (n == m) {
				handle->state = 1;
			} else {
				handle->remain = n - m;
				handle->frame_state = 1;
			}
		} else {
			m = stream_add(handle, handle->audio, buf, (len-ret));
			handle->remain = n - m;
			handle->frame_state = 1;
			buf += m;
			ret += m;
		}
		break;
	default:
		/* reset to start of frame */
		handle->state = 1;
		PRINTF("unknown mpeg state 0x%.2x\n", type);
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
			PRINTF("state 1\n");
			if (buf[0] == 0)
				handle->state = 2;
			break;
		case 2:
			PRINTF("state 2\n");
			if (buf[0] == 0)
				handle->state = 3;
			else
				handle->state = 1;
			break;
		case 3:
			PRINTF("state 3\n");
			if (buf[0] == 1)
				handle->state = 4;
			else if (buf[0] != 0)
				handle->state = 1;
			break;
		case 4:
			PRINTF("state 4\n");
			PRINTF("calling parse_frame() at offset %d\n", ret);
			n = parse_frame(handle, buf, len-ret, 0);
			handle->bytes += n;
			ret += n - 1;
			buf += n - 1;
			PRINTF("parse_frame() returned %d at offset %d\n",
			       n, ret);
			if (handle->state != 1) {
				PRINTF("breaking out of state 4\n");
				handle->bytes++;
				ret++;
				buf++;
				goto out;
			}
			break;
		default:
			/* reset to start of frame */
			PRINTF("unknown state %d\n", handle->state);
			handle->state = 1;
			break;
		}
		handle->bytes++;
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
start_stream(demux_handle_t *handle)
{
	int n;
	char *stream_buf;
	stream_t *video, *audio;

	if ((stream_buf=malloc(handle->size)) == NULL)
		return -1;
	memset(stream_buf, 0, handle->size);

	n = handle->size / 2;
	if ((video=stream_init(stream_buf, n)) == NULL)
		goto err;
	if ((audio=stream_init(stream_buf+n, n)) == NULL)
		goto err;

	if ((handle->spu_buf=malloc(64*1024)) == NULL)
		goto err;
	handle->spu_len = 0;

	audio->attr = &handle->attr.audio;
	video->attr = &handle->attr.video;

	handle->attr.audio.bufsz = n;
	handle->attr.video.bufsz = n;

	handle->audio = audio;
	handle->video = video;

	handle->state = 1;

	return 0;

 err:
	if (stream_buf)
		free(stream_buf);

	return -1;
}
