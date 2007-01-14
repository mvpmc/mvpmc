/*
 *  Copyright (C) 2004, 2005, 2006, BtB, Jon Gettler
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

/** \file mvp_av.h
 * MediaMVP Audio/Video Interface Library.  This library is used to control
 * the audio/video hardware on the MediaMVP.
 */

#ifndef MVP_AV_H
#define MVP_AV_H

#include <stdint.h>

#if !defined(__cplusplus) && !defined(HAVE_TYPE_BOOL)
#define HAVE_TYPE_BOOL
/**
 * Boolean type.
 */
typedef enum {
	false = 0,
	true = 1
} bool;
#endif /* !__cplusplus && !HAVE_TYPE_BOOL */

/**
 * The demuxer can either be on or off.  This is used to distinguish between
 * running on a MediaMVP (where the demuxer is on) and some other system (where
 * the demuxer is off since it would not be useful).
 */
typedef enum {
	AV_DEMUX_ERROR = -1,
	AV_DEMUX_ON,		/**< demuxer should be enabled */
	AV_DEMUX_OFF,		/**< demuxer should be disabled */
} av_demux_mode_t;

/**
 * The mode indicates whether the MediaMVP is running in NTSC (North America,
 * Japan, etc) or PAL (Europe) mode.
 */
typedef enum {
	AV_MODE_NTSC = 0,	/**< NTSC video mode (North America, Japan) */
	AV_MODE_PAL = 1,	/**< PAL video mode (Europe) */
	AV_MODE_UNKNOWN = 2,
} av_mode_t;

/**
 * The video can be output via the SCART, composite, or s-video interfaces.
 */
typedef enum {
	AV_OUTPUT_SCART = 0,	/**< SCART video output */
	AV_OUTPUT_COMPOSITE = 1,/**< Composite video output */
	AV_OUTPUT_SVIDEO = 2,	/**< S-Video video output */
} av_video_output_t;

/**
 * In pass-through mode, AC3 audio files will be passed through the digital
 * audio output (SPDIF) unmodified.  To use this, you will need an external
 * decoder.
 */
typedef enum {
	AUD_OUTPUT_PASSTHRU = 0,/**< Digital audio pass through */
	AUD_OUTPUT_STEREO = 1,	/**< Analog audio output */
} av_passthru_t;

/**
 * The aspect ratio of the playing video.
 */
typedef enum {
	AV_VIDEO_ASPECT_4x3 = 0,	/**< 4:3 video */
	AV_VIDEO_ASPECT_16x9 = 1,	/**< 16:9 (widescreen) video */
} av_video_aspect_t;

/**
 * The aspect ratio of the TV can be 4:3, 4:3 center-cut-out, or 16:9.
 */
typedef enum {
	AV_TV_ASPECT_4x3 = 0,		/**< 4:3 */
	AV_TV_ASPECT_4x3_CCO = 2,	/**< 4:3 center-cut-out */
	AV_TV_ASPECT_16x9 = 1		/**< 16:9 */
} av_tv_aspect_t;

/**
 * The widescreen signalling tells WSS compatible TVs what mode to run in.
 */
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

/**
 * Video event types.
 */
typedef enum {
    	VID_EVENT_ASPECT,
	VID_EVENT_SUBTITLES
} eventq_type_t;

/** TV is 16:9 widescreen */
#define IS_16x9(x) ((x)== AV_TV_ASPECT_16x9)
/** TV is 4:3 */
#define IS_4x3(x) ((x)== AV_TV_ASPECT_4x3 || (x) == AV_TV_ASPECT_4x3_CCO)

/**
 * Audio stream types.
 */
typedef enum {
	AV_AUDIO_STREAM_ES,	/**< Elementary Stream */
	AV_AUDIO_STREAM_PCM,	/**< PCM Audio Stream */
	AV_AUDIO_STREAM_PES,	/**< Packetized Elementary Stream */
	AV_AUDIO_STREAM_MPEG1,	/**< MPEG 1 Audio*/
	AV_AUDIO_STREAM_UNKNOWN,
} av_audio_stream_t;

/**
 * Audio output type
 */
typedef enum {
	AV_AUDIO_MPEG = 0,	/**< MPEG Audio */
	AV_AUDIO_PCM,		/**< PCM Audio */
	AV_AUDIO_AC3,		/**< Digital AC3 Audio */
	AV_AUDIO_CLOSE		/* Close Audio Device*/
} av_audio_output_t;

/**
 * Hardware sync data
 */
typedef struct {
	uint64_t stc;	/**< System Time Clock */
	uint64_t pts;	/**< Presentation Time Stamp */
} pts_sync_data_t;

typedef struct {
        int nleft;
        int state;
} vid_state_regs_t;

/**
 * System Time Clock
 */
typedef struct {
	int hour;	/**< hour */
	int minute;	/**< minute */
	int second;	/**< second */
} av_stc_t;

/**
 * Hardware volume setting.
 */
typedef struct {
	unsigned char front_left;	/**< Front left channel */
	unsigned char front_right;	/**< Front right channel */
	unsigned char rear_left;	/**< Rear left channel */
	unsigned char rear_right;	/**< Rear right channel */
	unsigned char center;		/**< Front center channel */
	unsigned char lfe;		/**< Subwoofer */
} av_volume_t;

/**
 * Video hardware state
 */
typedef struct {
	bool mute;	/**< Audio is muted */
	bool pause;	/**< Audio and video are paused */
	bool ffwd;	/**< Video is being played back at double speed */
} av_state_t;

#define PTS_HZ 90000	/**< Presentation time stamp clock frequency */

/**
 * Initialize the MedaiMVP audio/video hardware.
 * \retval AV_DEMUX_ERROR error
 * \retval AV_DEMUX_ON demuxer should be enabled
 * \retval AV_DEMUX_OFF demuxer should be disabled
 */
extern av_demux_mode_t av_init(void);

/**
 * Attach the framebuffer to the OSD.
 * \retval 0 success
 * \retval -1 error
 */
extern int av_attach_fb(void);

/**
 * Begin audio and video playback.
 * \retval 0 success
 * \retval -1 error
 */
extern int av_play(void);

/**
 * Get the file descriptor for the audio hardware device.
 * \return Audio hardware file descriptor. 
 */
extern int av_get_audio_fd(void);

/**
 * Get the file descriptor for the video hardware device.
 * \return Video hardware file descriptor. 
 */
extern int av_get_video_fd(void);

/**
 * Set the mode of the audio hardware.
 * \param audio_mode The audio mode to use.
 * \retval 0 success
 * \retval -1 error
 */
extern int av_set_audio_type(int audio_mode);

/**
 * Stop audio and video playback.
 * \retval 0 success
 * \retval -1 error
 */
extern int av_stop(void);

/**
 * Toggle pausing the audio and video playback.
 * \retval 0 playback is now paused
 * \retval 1 playback is now not paused
 * \retval -1 error
 */
extern int av_pause(void);

/**
 * Move the video image around on the screen.
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param video_mode
 * \retval 0 success
 * \retval -1 error
 */
extern int av_move(int x, int y, int video_mode);

/**
 * Toggle between fast forward and normal speed playback.
 * \retval 0 playback is now at normal speed
 * \retval 1 playback is now at fast forward speed
 * \retval -1 error
 */
extern int av_ffwd(void);

/**
 * Toggle between muted and audible audio.
 * \retval 0 audio is now audible
 * \retval 1 audio is now muted
 * \retval -1 error
 */
extern int av_mute(void);

/**
 * Mute or unmute the audio.
 * \param state 1 to mute, 0 to unmute
 * \retval 0 success
 * \retval -1 error
 */
extern int av_set_mute(bool state);

/**
 * Reset the audio and video hardware.
 * \retval 0 success
 * \retval -1 error
 */
extern int av_reset(void);

/**
 * Reset the audio and video STC (system time clock)
 * \retval 0 success
 * \retval -1 error
 */
extern int av_reset_stc(void);

/**
 * Get video hardware synchronization data.
 * \param[out] p pointer to sync data structure
 * \retval 0 success
 * \retval -1 error
 */
extern int av_get_video_sync(pts_sync_data_t *p);

/**
 * Get video hardware synchronization data.
 * \param[out] p pointer to return sync data
 * \retval 0 success
 * \retval -1 error
 */
extern int av_get_audio_sync(pts_sync_data_t *p);

/**
 * Get the current hardware STC (system time clock)
 * \param stc pointer used to return STC data
 * \retval 0 success
 * \retval -1 error
 */
extern int av_current_stc(av_stc_t *stc);

/**
 * Delay the video for some number of microseconds.
 * \param usec number of microseconds to delay the video
 * \retval 0 success
 * \retval -1 error
 */
extern int av_delay_video(int usec);

extern int vid_event_add(unsigned int pts, eventq_type_t type, void * info);
extern int vid_event_wait_next(eventq_type_t * type, void **info);

/**
 * Return the current video mode.
 * \retval AV_MODE_NTSC NTSC
 * \retval AV_MODE_PAL PAL
 */
extern av_mode_t av_get_mode(void);

/**
 * Return the current video output device.
 * \retval AV_OUTPUT_SCART SCART
 * \retval AV_OUTPUT_COMPOSITE composite
 * \retval AV_OUTPUT_SVIDEO s-video
 */
extern av_video_output_t av_get_output(void);

/**
 * Return the current aspect ratio setting for the TV.
 * \retval AV_TV_ASPECT_4x3 4:3
 * \retval AV_TV_ASPECT_4x3_CCO 4:3 center-cut-out
 * \retval AV_TV_ASPECT_16x9 16:9
 */
extern av_tv_aspect_t av_get_tv_aspect(void);

/**
 * Set the video mode.
 * \param mode AV_MODE_NTSC or AV_MODE_PAL
 * \retval 0 success
 * \retval -1 error
 */
extern int av_set_mode(av_mode_t mode);

/**
 * Set the video output device.
 * \param device the video output device
 * \retval 0 success
 * \retval -1 error
 */
extern int av_set_output(av_video_output_t device);

/**
 * Set the TV aspect ratio.
 * \param ratio the aspect ratio of the TV
 * \retval 0 success
 * \retval -1 error
 */
extern int av_set_tv_aspect(av_tv_aspect_t ratio);

/**
 * Toggle the MediaMVP LED on and off
 * \param on 1 to turn the LED on, 0 to turn it off
 * \retval 0 success
 * \retval -1 error
 */
extern int av_set_led(int on);

extern int av_set_pcm_param(unsigned long rate, int type, int channels,
			    bool big_endian, int bits);

/**
 * Set the audio output mode.
 * \param type output MPEG, PCM, or AC3 audio
 * \retval 0 success
 * \retval -1 error
 */
extern int av_set_audio_output(av_audio_output_t type);

/**
 * Synchronize the audio and video hardware devices.
 * \retval 0 success
 * \retval -1 error
 */
extern int av_sync(void);

extern av_wss_aspect_t av_set_video_aspect(av_video_aspect_t wide, int afd);
extern av_video_aspect_t av_get_video_aspect(void);

extern int av_get_video_status(void);
extern int av_get_audio_status(void);

/**
 * Blank the video display.
 * \retval 0 success
 * \retval -1 error
 */
extern int av_video_blank(void);

/**
 * Set the video STC (system time clock)
 * \param stc system time clock
 * \retval 0 success
 * \retval -1 error
 */
extern int av_set_video_stc(uint64_t stc);

/**
 * Set the audio STC (system time clock)
 * \param stc system time clock
 * \retval 0 success
 * \retval -1 error
 */
extern int av_set_audio_stc(uint64_t stc);

/**
 * Get the audio/video state.
 * \param[out] state return the audio/video state
 * \retval 0 success
 * \retval -1 error
 */
extern int av_get_state(av_state_t *state);

/**
 * Shut down the audio/video hardware.
 * \retval 0 success
 * \retval -1 error
 */
extern int av_deactivate(void);

/**
 * Get the current volume level.
 * \return -1 error, volume level on success
 */
extern int av_get_volume(void);

/**
 * Set the volume level.
 * \param volume volume level
 * \retval 0 success
 * \retval -1 error
 */
extern int av_set_volume(int volume);

/**
 * Display the color test bars.
 * \retval 0 success
 * \retval -1 error
 */
extern int av_colorbars(bool on);

/**
 * Determine if the audio and video hardware buffers are empty.
 * \retval 0 not empty
 * \retval 1 empty
 * \retval -1 error
 */
extern int av_empty(void);

#define AV_VOLUME_MAX	255	/**< maximum volume */
#define AV_VOLUME_MIN	0	/**< minimum volume */

/*
 * mvpstb_mod api's
 */

/**
 * Read kernel data.
 * \param memaddr kernel memory address
 * \param[out] buffaddr address to store data
 * \param size size of buffaddr
 * \retval 0 success
 * \retval <0 error
 */
extern int kern_read(unsigned long memaddr, void *buffaddr, unsigned int size);

/**
 * Write kernel data.
 * \param memaddr kernel memory address
 * \param buffaddr data to be written
 * \param size size of buffaddr
 * \retval 0 success
 * \retval <0 error
 */
extern int kern_write(unsigned long memaddr, void *buffaddr, unsigned int size);
/**
 * Read a DCR register.
 * \param regaddr register address
 * \param[out] data address to store data
 * \retval 0 success
 * \retval <0 error
 */
extern int dcr_read(unsigned long regaddr, unsigned int *data);

/**
 * Write a DCR register.
 * \param regaddr register address
 * \param data data to be written
 * \retval 0 success
 * \retval <0 error
 */
extern int dcr_write(unsigned long regaddr, unsigned int data);

/**
 * Get video system time code.
 * \param[out] vstc output buffer
 * \retval 0 success
 * \retval <0 error
 */
extern int mvpstb_get_vid_stc(unsigned long long *vstc);

/**
 * Get video presentation time stamp.
 * \param[out] vpts output buffer
 * \retval 0 success
 * \retval <0 error
 */
extern int mvpstb_get_vid_pts(unsigned long long *vpts);

/**
 * Get audio system time code.
 * \param[out] astc output buffer
 * \retval 0 success
 * \retval <0 error
 */
extern int mvpstb_get_aud_stc(unsigned long long *astc);

/**
 * Get audio presentation time stamp.
 * \param[out] apts output buffer
 * \retval 0 success
 * \retval <0 error
 */
extern int mvpstb_get_aud_pts(unsigned long long *apts);

/**
 * Enable or disable video sync.
 * \param on 1 to enable, 0 to disable
 * \retval 0 success
 * \retval <0 error
 */
extern int mvpstb_set_video_sync(int on);

/**
 * Enable or disable audio sync.
 * \param on 1 to enable, 0 to disable
 * \retval 0 success
 * \retval <0 error
 */
extern int mvpstb_set_audio_sync(int on);

/**
 * Start audio/video auditing.
 * \param interval_ms audit interval in milliseconds
 * \retval 0 success
 * \retval <0 error
 */
extern int mvpmod_start_audit(unsigned long interval_ms);

/**
 * Stop audio/video auditing.
 * \retval 0 success
 * \retval <0 error
 */
extern int mvpmod_stop_audit(void);

/**
 * Set the offset for letterboxed output.
 * \param offset offset in lines per field
 * \retval 0 success
 * \retval <0 error
 */
extern int mvpstb_set_lbox_offset(unsigned int offset);

/**
 * Get the offset for letterboxed output.
 * \param[out] offset offset in lines per field
 * \retval 0 success
 * \retval <0 error
 */
extern int mvpstb_get_lbox_offset(unsigned int *offset);

/**
 * Determine if the end of audio hardware buffer has been reached.
 * \retval 0 not empty
 * \retval 1 empty
 * \retval -1 error
 */
extern int mvpstb_audio_end(void);

/**
 * Determine if the end of video hardware buffer has been reached.
 * \retval 0 not empty
 * \retval 1 empty
 * \retval -1 error
 */
extern int mvpstb_video_end(void);

#endif /* MVP_AV_H */
