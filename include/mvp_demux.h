/*
 *  $Id$
 *
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

#ifndef MVP_DEMUX_H
#define MVP_DEMUX_H

typedef struct {
	int width;
	int height;
	int aspect;
	int frame_rate;
} video_info_t;

typedef struct {
	unsigned int frames;
	unsigned int bytes;
	unsigned int full_count;
	unsigned int empty_count;
	unsigned int drain_count;
	unsigned int fill_count;
	union {
		video_info_t video;
	} info;
} stream_stats_t;

typedef struct {
	unsigned int display;
	unsigned int existing;
	unsigned int type;
	stream_stats_t stats;
	unsigned int bufsz;
} stream_attr_t;

typedef struct {
	stream_attr_t audio;
	stream_attr_t video;
} demux_attr_t;

typedef struct demux_handle_s demux_handle_t;

extern demux_handle_t *demux_init(unsigned int);
extern int demux_destroy(demux_handle_t*);

extern int demux_put(demux_handle_t*, char*, int);
extern int demux_get_audio(demux_handle_t*, char*, int);
extern int demux_get_video(demux_handle_t*, char*, int);
extern int demux_write_audio(demux_handle_t*, int);
extern int demux_write_video(demux_handle_t*, int);

extern demux_attr_t *demux_get_attr(demux_handle_t*);

extern int demux_set_audio_stream(demux_handle_t*, unsigned int);
extern int demux_set_video_stream(demux_handle_t*, unsigned int);

extern int demux_buffer_resize(demux_handle_t *handle);
extern int demux_empty(demux_handle_t *handle);
extern int demux_flush(demux_handle_t *handle);
extern void demux_seek(demux_handle_t *handle);

#endif /* MVP_DEMUX_H */
