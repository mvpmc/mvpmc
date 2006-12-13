/*
 *  Copyright (C) 2004-2006, Jon Gettler
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

#ifndef DEMUX_H
#define DEMUX_H

#define SPU_MAX		32

typedef int(* parser_callback_t)(unsigned char* /*pRingBuf*/,
				 unsigned int /*tail*/,
				 unsigned int /*head*/, 
				 unsigned int /* buf size */,
				 void * /*pParserData */,
				 demux_handle_t * /*handle*/);

				 

typedef struct {
	unsigned char *buf;
	unsigned int head;
	unsigned int tail;
	unsigned int size;
	unsigned int parser_tail;
	unsigned int seeking_head;
	int seeking_head_valid;
	int status;
	stream_attr_t *attr;
	gop_t *gop;
	parser_callback_t parser_callback;
	void * parser_data;
	pthread_mutex_t * ptr_tail_mutex;
} stream_t;

typedef struct {
	unsigned char *buf;
	int bufsz;
	int len;
	int size;
	int data_size;
	spu_item_t item[32];
	int inuse;
} spu_t;

struct demux_handle_s {
	demux_attr_t attr;
	stream_t *audio;
	stream_t *video;
	int state;
	int frame_state;
    int allow_iframe;
	int remain;
	unsigned int size;
	unsigned char buf[16];
	int bufsz;
	int headernum;
	int seeking;
	unsigned int bytes;
	spu_t spu[SPU_MAX];
	int spu_current;
	unsigned char *spu_buf;
	int spu_len;
	int width;
	int height;
	void *stream_buf;
};

#define pack_start_code			0xBA
#define system_header_start_code	0xBB
#define MPEG_program_end_code		0xB9
#define program_stream_map		0xBC
#define private_stream_1		0xBD
#define private_stream_2		0xBF
#define padding_stream			0xBE
#define user_data_start_code		0xB2

#define audio_stream_0			0xc0
#define audio_stream_1			0xc1
#define audio_stream_2			0xc2
#define audio_stream_3			0xc3
#define audio_stream_4			0xc4
#define audio_stream_5			0xc5
#define audio_stream_6			0xc6
#define audio_stream_7			0xc7
#define audio_stream_8			0xc8
#define audio_stream_9			0xc9
#define audio_stream_A			0xcA
#define audio_stream_B			0xcB
#define audio_stream_C			0xcC
#define audio_stream_D			0xcD
#define audio_stream_E			0xcE
#define audio_stream_F			0xcF

#define video_stream_0			0xe0
#define video_stream_1			0xe1
#define video_stream_2			0xe2
#define video_stream_3			0xe3
#define video_stream_4			0xe4
#define video_stream_5			0xe5
#define video_stream_6			0xe6
#define video_stream_7			0xe7
#define video_stream_8			0xe8
#define video_stream_9			0xe9
#define video_stream_A			0xeA
#define video_stream_B			0xeB
#define video_stream_C			0xeC
#define video_stream_D			0xeD
#define video_stream_E			0xeE
#define video_stream_F			0xeF

#define UNKNOWN		0
#define MPEG1		1
#define MPEG2		2

extern int start_stream(demux_handle_t *handle);
extern int add_buffer(demux_handle_t *handle, void *buf, int len);
extern int parse_frame(demux_handle_t *handle, unsigned char *buf,
		       int len, int type);
extern int stream_drain(stream_t *stream, void *buf, int max);
extern int stream_drain_fd(stream_t *stream, int fd);
extern int stream_resize(stream_t *stream, void *start, int size);

extern inline void
register_stream(stream_t *stream, int id, stream_type_t type)
{
	int i, existing;

	existing = stream->attr->existing;

	for (i=0; i<existing; i++) {
		if (stream->attr->ids[i].id == id)
			return;
	}

	stream->attr->ids[i].id = id;
	stream->attr->ids[i].type = type;
	stream->attr->existing++;
}

#endif /* DEMUX_H */
