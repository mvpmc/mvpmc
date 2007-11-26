/*
 *  Copyright (C) 2004,2005,2006 Jon Gettler
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

/** \file mvp_demux.h
 * MPEG stream demux library.  This library is capable of demuxing an MPEG
 * stream into the audio and video components needed by the MediaMVP
 * hardware.
 */

#ifndef MVP_DEMUX_H
#define MVP_DEMUX_H

#define AUDIO_MODE_ES		1	/**< Elementary Stream */
#define AUDIO_MODE_MPEG2_PES	2	/**< Packetized Elementary Stream */
#define AUDIO_MODE_MPEG1_PES	3	/**< Packetized Elementary Stream */
#define AUDIO_MODE_AC3		4	/**< Digital AC3 Stream */
#define AUDIO_MODE_PCM		5	/**< PCM Stream */

/**
 * Group of Pictures
 */
typedef struct {
	unsigned char hour;
	unsigned char minute;
	unsigned char second;
	unsigned char frame;
	unsigned int offset;
	unsigned int pts;
} gop_t;

/**
 * Video Information
 */
typedef struct {
	int width;
	int height;
	int aspect;
	int frame_rate;
	int afd;
} video_info_t;

/**
 * Stream statistics
 */
typedef struct {
	unsigned int frames;
	unsigned int bytes;
	unsigned int cur_bytes;
	unsigned int full_count;
	unsigned int empty_count;
	unsigned int drain_count;
	unsigned int fill_count;
	union {
		video_info_t video;
	} info;
} stream_stats_t;

/**
 * MPEG stream type
 */
typedef enum {
	STREAM_MPEG,
	STREAM_AC3,
	STREAM_PCM,
} stream_type_t;

typedef struct {
    int aspect;
    int afd;
} aspect_change_t;
	
/**
 * MPEG stream information
 */
typedef struct {
	unsigned int id;
	stream_type_t type;
} stream_info_t;

/**
 * MPEG stream attributes
 */
typedef struct {
	unsigned int current;
	unsigned int existing;
	stream_info_t ids[16];
	unsigned int type;
	stream_stats_t stats;
	unsigned int bufsz;
} stream_attr_t;

/**
 * SPU (DVD subpicture) data
 */
typedef struct {
	int bytes;
	int frames;
} spu_attr_t;

/**
 * demuxer attributes
 */
typedef struct {
	stream_attr_t audio;
	stream_attr_t video;
	spu_attr_t spu[32];
	int Bps;
	gop_t gop;
	int gop_valid;
	int ac3_audio;
} demux_attr_t;

/**
 * SPU (DVD subpicture) item
 */
typedef struct {
	unsigned char *data;
	int size;
	int x;
	int y;
	int w;
	int h;
	unsigned int start;
	unsigned int end;
	unsigned int color;
	unsigned int alpha;
	unsigned int line[2];
} spu_item_t;

typedef struct demux_handle_s demux_handle_t;

/**
 * Initialize a demuxer.
 * \param size Amount of memory, in bytes, that the demuxer is allowed to use.
 * \return handle to the demuxer, or NULL on an error
 */
extern demux_handle_t *demux_init(unsigned int size);

/**
 * Destroy a demuxer.
 * \param handle handle to a demuxer
 * \retval 0 success
 * \retval -1 error
 */
extern int demux_destroy(demux_handle_t *handle);

/**
 * Set the pixel dimensions of the TV
 * \param handle handle to a demuxer
 * \param w width of the TV
 * \param h height of the TV
 * \retval 0 success
 * \retval -1 error
 */
extern int demux_set_display_size(demux_handle_t *handle, int w, int h);

/**
 * Add MPEG stream data to a demuxer
 * \param handle handle to a demuxer
 * \param buf MPEG stream data buffer
 * \param len size of buf in bytes
 * \return number of bytes consumed
 */
extern int demux_put(demux_handle_t *handle, void *buf, int len);

/**
 * Retrieve audio data from a demuxer.
 * \param handle handle to a demuxer
 * \param[out] buf data buffer where audio data should be stored
 * \param max maximum number of bytes to retrieve
 * \return number of bytes retrieved
 */
extern int demux_get_audio(demux_handle_t *handle, void *buf, int max);

/**
 * Retrieve video data from a demuxer.
 * \param handle handle to a demuxer
 * \param[out] buf data buffer where video data should be stored
 * \param max maximum number of bytes to retrieve
 * \return number of bytes retrieved
 */
extern int demux_get_video(demux_handle_t *handle, void *buf, int max);

/**
 * Write audio data from a demuxer to a file descriptor.
 * \param handle handle to a demuxer
 * \param fd file descriptor
 * \return number of bytes written
 */
extern int demux_write_audio(demux_handle_t *handle, int fd);

/**
 * Write audio data from a demuxer to a file descriptor Just In Time
 * \param handle handle to a demuxer
 * \param fd file descriptor
 * \param stc current video STC (32 LSBits of it)
 * \param mode a bitwise or of the following flags:
 *                 1 - Bring audio in sync after a seek by throwing some away
 *                 2 - Try to keep audio in sync at other times
 * \param flags Indicate A/V action to be performed by caller:
 *        1 - video_pause
 *        2 - video_unpause
 *        4 - video_pause_duration - Pause video for the specified duration.
 *        8 - audio_stall - Wait a while before re-trying to send audio.
 * \param duration Duration of any video_pause_duration/audio_stall in
 *                 milliseconds
 * \return number of bytes written
 */
extern int demux_jit_write_audio(demux_handle_t *handle, int fd,
                                 unsigned int pts, int mode, 
				 int *flags, int *duration);

/**
 * Write video data from a demuxer to a file descriptor.
 * \param handle handle to a demuxer
 * \param fd file descriptor
 * \return number of bytes written
 */
extern int demux_write_video(demux_handle_t *handle, int fd);

/**
 * Retrieve the attribute structure for a demuxer.
 * \param handle handle to a demuxer
 * \return demuxer attributes
 */
extern demux_attr_t *demux_get_attr(demux_handle_t *handle);

/**
 * Select the active audio stream
 * \param handle handle to a demuxer
 * \param id stream ID to select
 * \retval -1 error
 * \retval 0 stream id already selected
 * \retval other type of the stream id selected
 */
extern int demux_set_audio_stream(demux_handle_t *handle, unsigned int id);

/**
 * Select the active video stream
 * \param handle handle to a demuxer
 * \param id stream ID to select
 * \retval -1 error
 * \retval 0 stream id already selected
 * \retval other type of the stream id selected
 */
extern int demux_set_video_stream(demux_handle_t *handle, unsigned int id);

#if 0
extern int demux_buffer_resize(demux_handle_t *handle);
#endif

/**
 * Check if the demux buffers are empty.
 * \param handle handle to a demuxer
 * \retval 1 buffers are empty
 * \retval 0 buffers are not empty
 */
extern int demux_empty(demux_handle_t *handle);

/**
 * Flush all stream data from the demuxer.
 * \param handle handle to a demuxer
 * \retval 0 success
 * \retval -1 error
 */
extern int demux_flush(demux_handle_t *handle);

/**
 * Place the demuxer into seek mode, so that it consumes all new data until
 * an I-frame is seen.
 * \param handle handle to a demuxer
 */
extern void demux_seek(demux_handle_t *handle);

/**
 * Reset the demuxer to the default, initialized state.
 * \param handle handle to a demuxer
 * \retval 0 success
 * \retval -1 error
 */
extern int demux_reset(demux_handle_t *handle);

/**
 * Reset all the attributes of the demuxer, effectively clearing all
 * MPEG stream data.
 * \param handle handle to a demuxer
 * \retval 0 success
 * \retval -1 error
 */
extern int demux_attr_reset(demux_handle_t *handle);

/**
 * Set the current subpicture stream id.
 * \param handle handle to a demuxer
 * \param id stream id
 * \retval 0 success
 * \retval -1 error
 */
extern int demux_spu_set_id(demux_handle_t *handle, int id);

/**
 * Get the current subpicture stream id.
 * \param handle handle to a demuxer
 * \return current subpicture stream id
 */
extern int demux_spu_get_id(demux_handle_t *handle);

/**
 * Retrieve the next subpicture for the current stream.
 * \param handle handle to a demuxer
 * \return subpicture data
 */
extern spu_item_t* demux_spu_get_next(demux_handle_t *handle);

/**
 * Decompress a subpicture RLE bitmap
 * \param handle handle to a demuxer
 * \param spu subpicture data
 * \return malloc()'d buffer containing a decompressed subpicture
 */
extern char* demux_spu_decompress(demux_handle_t *handle, spu_item_t *spu);

#endif /* MVP_DEMUX_H */
