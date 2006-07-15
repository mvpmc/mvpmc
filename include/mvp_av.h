/*
 *  $Id$
 *
 *  Copyright (C) 2004, 2005, BtB, Jon Gettler
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

#ifndef MVP_AV_H
#define MVP_AV_H

#include <stdint.h>

typedef enum {
	AV_DEMUX_ERROR = -1,
	AV_DEMUX_ON,
	AV_DEMUX_OFF,
} av_demux_mode_t;

typedef enum {
	AV_MODE_NTSC = 0,
	AV_MODE_PAL = 1,
	AV_MODE_UNKNOWN = 2,
} av_mode_t;

typedef enum {
	AV_OUTPUT_SCART = 0,
	AV_OUTPUT_COMPOSITE = 1,
	AV_OUTPUT_SVIDEO = 2,
} av_video_output_t;

typedef enum {
	AUD_OUTPUT_PASSTHRU = 0,
	AUD_OUTPUT_STEREO = 1,
} av_passthru_t;

typedef enum {
    AV_VIDEO_ASPECT_4x3 = 0,
    AV_VIDEO_ASPECT_16x9 = 1
} av_video_aspect_t;
typedef enum {
	AV_TV_ASPECT_4x3 = 0,
	AV_TV_ASPECT_4x3_CCO = 2,
	AV_TV_ASPECT_16x9 = 1
} av_tv_aspect_t;

typedef enum {
    	WSS_ASPECT_UNKNOWN = 0,
    	WSS_ASPECT_FULL_4x3 = 8,
	WSS_ASPECT_BOX_14x9_CENTRE = 1,
	WSS_ASPECT_BOX_14x9_TOP = 2,
	WSS_ASPECT_BOX_16x9_CENTRE = 11,
	WSS_ASPECT_BOX_16x9_TOP = 4,
	WSS_ASPECT_BOX_GT_16x9_CENTRE = 13,
	WSS_ASPECT_FULL_4x3_PROTECT_14x9 = 14,
	WSS_ASPECT_FULL_16x9 = 7
} av_wss_aspect_t;

#define IS_16x9(x) ((x)== AV_TV_ASPECT_16x9)
#define IS_4x3(x) ((x)== AV_TV_ASPECT_4x3 || (x) == AV_TV_ASPECT_4x3_CCO)

typedef enum {
	AV_AUDIO_STREAM_ES,
	AV_AUDIO_STREAM_PCM,
	AV_AUDIO_STREAM_PES,
	AV_AUDIO_STREAM_MPEG1,
	AV_AUDIO_STREAM_UNKNOWN,
} av_audio_stream_t;

typedef enum {
	AV_AUDIO_MPEG = 0,
	AV_AUDIO_PCM,
	AV_AUDIO_AC3
} av_audio_output_t;

typedef struct {
        uint64_t stc;
        uint64_t pts;
} pts_sync_data_t;

typedef struct {
        int nleft;
        int state;
} vid_state_regs_t;

typedef struct {
        int hour;
        int minute;
        int second;
} av_stc_t;

typedef struct {
	unsigned char front_left;
	unsigned char front_right;
	unsigned char rear_left;
	unsigned char rear_right;
	unsigned char center;
	unsigned char lfe;
} av_volume_t;

typedef struct {
	int mute;
	int pause;
	int ffwd;
} av_state_t;

/* Presentation time stamp clock frequency */
#define PTS_HZ 90000

extern int av_init(void);
extern int av_video_set_nonblock(int);
extern int av_audio_set_nonblock(int);
extern int av_attach_fb(void);
extern int av_play(void);
extern int av_audio_fd(void);
extern int av_video_fd(void);
extern int av_set_audio_type(int audio_mode);
extern int av_stop(void);
extern int av_pause(void);
extern int av_move(int x, int y, int video_mode);
extern int av_ffwd(void);
extern int av_mute(void);
extern int av_reset(void);
extern int av_reset_stc(void);
extern int get_video_sync(pts_sync_data_t *p);
extern int get_audio_sync(pts_sync_data_t *p);
extern int av_current_stc(av_stc_t *stc);
extern int av_delay_video(int usec);

extern void av_wss_update_aspect(av_wss_aspect_t aspect);
extern void av_wss_init();
extern void av_wss_visible(int isVisible);
extern void av_wss_redraw();

extern av_mode_t av_get_mode(void);
extern int av_get_output(void);
extern av_tv_aspect_t av_get_tv_aspect(void);

extern int av_set_mode(av_mode_t);
extern int av_set_output(int);
extern int av_set_tv_aspect(av_tv_aspect_t);

extern int av_set_led(int);

extern int av_set_pcm_param(unsigned long rate, int type, int channels,
			    int big_endian, int bits);
extern int av_set_audio_output(av_audio_output_t type);

extern int av_sync(void);

extern av_wss_aspect_t av_set_video_aspect(av_video_aspect_t wide, int afd);
extern av_video_aspect_t av_get_video_aspect(void);

extern int av_get_video_status(void);
extern int av_get_audio_status(void);

extern int av_video_blank(void);

extern int set_video_stc(uint64_t stc);
extern int set_audio_stc(uint64_t stc);

extern int av_get_state(av_state_t *state);

extern int av_deactivate(void);

extern int av_get_volume(void);
extern int av_set_volume(int volume);

extern int av_colorbars(int on);

extern int av_empty(void);

#define AV_VOLUME_MAX	255
#define AV_VOLUME_MIN	0

/*
 * mvpstb_mod api's
 */
int kern_read(unsigned long memaddr, void *buffaddr, unsigned int size);
int kern_write(unsigned long memaddr, void *buffaddr, unsigned int size);
int dcr_read(unsigned long regaddr, unsigned int *data);
int dcr_write(unsigned long regaddr, unsigned int data);
int mvpstb_get_vid_stc(unsigned long long *vstc);
int mvpstb_get_vid_pts(unsigned long long *vpts);
int mvpstb_get_aud_stc(unsigned long long *astc);
int mvpstb_get_aud_pts(unsigned long long *apts);
int mvpstb_set_video_sync(int on);
int mvpstb_set_audio_sync(int on);
int mvpmod_start_audit(unsigned long interval_ms);
int mvpmod_stop_audit(void);
int mvpstb_set_lbox_offset(unsigned int offset);
int mvpstb_get_lbox_offset(unsigned int *offset);
int mvpstb_audio_end(void);
int mvpstb_video_end(void);

#endif /* MVP_AV_H */
