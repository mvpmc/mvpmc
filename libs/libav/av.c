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

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <string.h>

#include "mvp_av.h"
#include "stb.h"
#include "av_local.h"

static av_aspect_t letterbox = 0;
static int output = -1;

static av_audio_output_t audio_output = AV_AUDIO_MPEG;

/*
 * id, data input frequency, data output frequency
 */
static int pcmfrequencies[][3] = {{9 ,8000 ,32000},
				  {10,11025,44100},
				  {11,12000,48000},
				  {1 ,16000,32000},
				  {2 ,22050,44100},
				  {3 ,24000,48000},
				  {5 ,32000,32000},
				  {0 ,44100,44100},
				  {7 ,48000,48000},
				  {13,64000,32000},
				  {14,88200,44100},
				  {15,96000,48000}};

static int numfrequencies = sizeof(pcmfrequencies)/12;

/*
 * av_video_blank() - blank the video screen
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_video_blank(void)
{
	if (ioctl(fd_video, AV_SET_VID_FB, 1) != 0)
		return -1;

	return 0;
}

/*
 * av_sync() - synchronize the audio and video output devices
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_sync(void)
{
	if (ioctl(fd_video, AV_SET_VID_SYNC, VID_SYNC_AUD) != 0)
		return -1;
	if (ioctl(fd_audio, AV_SET_AUD_SYNC, 2) != 0)
		return -1;

	return 0;
}

/*
 * set_output_method() - setup the current output device
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
set_output_method(void)
{
	unsigned long scart;

	if (ioctl(fd_video, AV_CHK_SCART, &scart) != 0)
		return -1;

	switch (scart) {
	case 1:
	case 2:
	case 3:
		break;
	default:
		return -1;
		break;
	}

	if (output == -1)
		output = scart;

	if (ioctl(fd_video, AV_SET_VID_DISP_FMT, vid_mode) != 0)
		return -1;
	if (ioctl(fd_video, AV_SET_VID_SRC, 1) != 0)
		return -1;
	if (ioctl(fd_video, AV_SET_VID_OUTPUT, output) != 0)
		return -1;

	return 0;
}

/*
 * av_set_output() - set the video output method
 *
 * Arguments:
 *	method	- AV_OUTPUT_SVIDEO or AV_OUTPUT_COMPOSITE
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_set_output(int method)
{
	if (ioctl(fd_video, AV_SET_VID_OUTPUT, method) != 0)
		return -1;
	output = method;

	return 0;
}

/*
 * av_get_output() - get the video output method
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	AV_OUTPUT_SVIDEO or AV_OUTPUT_COMPOSITE
 */
int
av_get_output(void)
{
	return output;
}

/*
 * av_set_video_aspect() - set the aspect ratio of the video being played
 *
 * Arguments:
 *	wide	- AV_ASPECT_4x3 or AV_ASPECT_16x9
 *
 * Returns:
 *	0 on success
 */
int
av_set_video_aspect(av_aspect_t wide)
{
	letterbox = wide;

	return 0;
}

/*
 * av_get_video_aspect() - get the aspect ratio of the video being played
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	AV_ASPECT_4x3 or AV_ASPECT_16x9
 */
av_aspect_t
av_get_video_aspect(void)
{
	return letterbox;
}

/*
 * av_get_mode() - get the video output mode
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	AV_MODE_PAL or AV_MODE_NTSC
 */
av_mode_t
av_get_mode(void)
{
	return vid_mode;
}

/*
 * av_set_mode() - set the video output mode
 *
 * Arguments:
 *	mode	- AV_MODE_PAL or AV_MODE_NTSC
 *
 * Returns:
 *	0 if it succeeded in changing the output mode, -1 if it failed
 */
int
av_set_mode(av_mode_t mode)
{
	if ((mode != AV_MODE_PAL) && (mode != AV_MODE_NTSC))
		return -1;
	if (ioctl(fd_video, AV_SET_VID_DISP_FMT, mode) != 0)
		return -1;

	vid_mode = mode;

	return 0;
}

/*
 * av_set_aspect() - set the aspect ratio of the output device (ie, the TV)
 *
 * Arguments:
 *	ratio	- AV_ASPECT_4x3 or AV_ASPECT_16x9
 *
 * Returns:
 *	0 if it succeeded in changing the aspect ratio, -1 if it failed
 */
int
av_set_aspect(av_aspect_t ratio)
{
	if ((ratio != AV_ASPECT_4x3) && (ratio != AV_ASPECT_16x9))
		return -1;

	if (ioctl(fd_video, AV_SET_VID_RATIO, ratio) < 0)
		return -1;
	aspect = ratio;

	return 0;
}

/*
 * av_get_aspect() - get the aspect ratio of the output device (ie, the TV)
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	AV_ASPECT_4x3 or AV_ASPECT_16x9
 */
av_aspect_t
av_get_aspect(void)
{
	return aspect;
}

/*
 * av_set_audio_type() - set the audio stream type
 *
 * XXX: What does this do for PCM output?
 *
 * Arguments:
 *	mode	- AUDIO_MODE_MPEG1_PES or AUDIO_MODE_MPEG2_PES
 *
 * Returns:
 *	0 if it succeeded in changing the audio type, -1 if it failed
 */
int
av_set_audio_type(int mode)
{
	return ioctl(fd_audio, AV_SET_AUD_STREAMTYPE, mode);
}

/*
 * av_attach_fb() - attach framebuffer to OSD
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_attach_fb(void)
{
	if (ioctl(fd_video, AV_SET_VID_FB, 0) != 0)
		return -1;

	return 0;
}

/*
 * av_play() - allow audio and video to play
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_play(void)
{
	if (ioctl(fd_audio, AV_SET_AUD_UNPAUSE, 1) < 0)
		return -1;
	if (ioctl(fd_audio, AV_SET_AUD_MUTE, 0) < 0)
		return -1;

	av_sync();
	if (ioctl(fd_video, AV_SET_VID_PLAY, 0) != 0)
		return -1;
	av_sync();
	if (ioctl(fd_audio, AV_SET_AUD_PLAY, 0) != 0)
		return -1;

	paused = 0;
	muted = 0;
	ffwd = 0;

	return 0;
}


/*
 * av_pause() - toggle the pause state of the audio and video
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0	- audio/video is not paused
 *	1	- audio/video is paused
 *	-1	- pause failed
 */
int
av_pause(void)
{
	if (paused) {
		if (ioctl(fd_audio, AV_SET_AUD_UNPAUSE, 1) < 0)
			return -1;
		if (ioctl(fd_audio, AV_SET_AUD_MUTE, 0) < 0)
			return -1;
		av_attach_fb();
		av_play();
		av_sync();
		paused = 0;
		muted = 0;
	} else {
		if (ioctl(fd_audio, AV_SET_AUD_MUTE, 1) < 0)
			return -1;
		if (ioctl(fd_audio, AV_SET_AUD_PAUSE, 1) < 0)
			return -1;
		if (ioctl(fd_video, AV_SET_VID_PAUSE, 0) < 0)
			return -1;
		paused = 1;
	}

	return paused;
}

/*
 * av_mute() - toggle the mute state of the audio
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0	- audio is not muted
 *	1	- audio is muted
 *	-1	- mute failed
 */
int
av_mute(void)
{
	if (muted) {
		if (ioctl(fd_audio, AV_SET_AUD_MUTE, 0) < 0)
			return -1;
		muted = 0;
	} else {
		if (ioctl(fd_audio, AV_SET_AUD_MUTE, 1) < 0)
			return -1;
		muted = 1;
	}

	return muted;
}

/*
 * av_ffwd() - toggle the fast forward state of the video
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0	- video is in normal play mode
 *	1	- video is in fast forward mode
 *	-1	- fast forward failed
 */
int
av_ffwd(void)
{
	if (ffwd == 0)
		ffwd = 1;
	else
		ffwd = 0;

	if (ioctl(fd_video, AV_SET_VID_FFWD, ffwd) < 0)
		return -1;

	if (ffwd == 0) {
		if (ioctl(fd_video, AV_SET_VID_PLAY, 0) != 0)
			return -1;
		av_sync();
	}

	return ffwd;
}

/*
 * av_stop() - stop the audio and video
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_stop(void)
{
	if (ioctl(fd_audio, AV_SET_AUD_STOP, 0) < 0)
		return -1;
	if (ioctl(fd_video, AV_SET_VID_STOP, 0) < 0)
		return -1;

	if (ioctl(fd_audio, AV_SET_AUD_RESET, 0x11) < 0)
		return -1;
	if (ioctl(fd_video, AV_SET_VID_RESET, 0x11) < 0)
		return -1;

	return 0;
}

/*
 * av_reset() - reset the audio and video devices
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_reset(void)
{
	if (ioctl(fd_audio, AV_SET_AUD_RESET, 0x11) < 0)
		return -1;
	if (ioctl(fd_video, AV_SET_VID_RESET, 0x11) < 0)
		return -1;

	if (muted)
		if (ioctl(fd_audio, AV_SET_AUD_MUTE, 0) < 0)
			return -1;

	if (ioctl(fd_audio, AV_SET_AUD_PLAY, 0) != 0)
		return -1;
	av_sync();

	paused = 0;
	muted = 0;
	ffwd = 0;

	return 0;
}

int
get_audio_sync(pts_sync_data_t *p)
{
	int ret;

	if ((ret=ioctl(fd_audio, AV_GET_AUD_SYNC, p)) == 0) {
		p->stc = (p->stc >> 31 ) | (p->stc & 1);
		p->pts = (p->pts >> 31 ) | (p->pts & 1);
	}

	return ret;
}

int
get_video_sync(pts_sync_data_t *p)
{
	int ret;

	if ((ret=ioctl(fd_video, AV_GET_VID_SYNC, p)) == 0) {
		p->stc = (p->stc >> 31 ) | (p->stc & 1);
		p->pts = (p->pts >> 31 ) | (p->pts & 1);
	}

	return ret;
}

int
get_vid_state(vid_state_regs_t *p)
{
	return ioctl(fd_video, AV_GET_VID_STATE, p);
}

int
av_current_stc(av_stc_t *stc)
{
	pts_sync_data_t pts;
	int hour, minute, second;

	if (get_video_sync(&pts) == 0) {
		second = pts.stc / PTS_HZ;
		hour = second / 3600;
		minute = second / 60 - hour * 60;
		second = second % 60;

		stc->hour = hour;
		stc->minute = minute;
		stc->second = second;

		return 0;
	}

	return -1;
}

/*
 * av_move()
 *
 * Arguments:
 *	x		- location of video on x axis
 *	y		- location of video on y axis
 *	video_mode	- specify a video mode from the following
 *				0 - normal
 *				1 - ??
 *				2 - ??
 *				3 - quarter screen
 *				4 - eigth screen
 *				5 - zoom
 *				6 - ??
 */
int
av_move(int x, int y, int video_mode)
{
	vid_pos_regs_t pos_d;

	memset(&pos_d, 0, sizeof(pos_d));

	pos_d.dest.y = y;
	pos_d.dest.x = x;

	ioctl(fd_video, AV_SET_VID_POSITION, &pos_d);

	if (video_mode == 0)
		ioctl(fd_video, AV_SET_VID_MODE, letterbox);
	else
		ioctl(fd_video, AV_SET_VID_MODE, video_mode);

	if (video_mode == 0)
		ioctl(fd_video, AV_SET_VID_SRC, 1);

	return 0;
}

/*
 * av_set_audio_output() - change the audio output device
 *
 * Arguments:
 *	type	- AV_AUDIO_MPEG or AV_AUDIO_PCM
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_set_audio_output(av_audio_output_t type)
{
	if (audio_output != type) {
		int mix[5] = { 0, 2, 7, 1, 0 };

		close(fd_audio);

		switch (type) {
		case AV_AUDIO_MPEG:
			if ((fd_audio=open("/dev/adec_mpg",
					   O_RDWR|O_NONBLOCK)) < 0)
				return -1;
			printf("opened /dev/adec_mpg\n");
			if (ioctl(fd_audio, AV_SET_AUD_SRC, 1) < 0)
				return -1;
			if (ioctl(fd_audio, AV_SET_AUD_STREAMTYPE, 2) < 0)
				return -1;
			if (ioctl(fd_audio, AV_SET_AUD_CHANNEL, 0) < 0)
				return -1;
			ioctl(fd_audio, AV_SET_AUD_BYPASS, 1);

			av_sync();

			if (ioctl(fd_audio, AV_SET_AUD_PLAY, 0) != 0)
				return -1;

			break;
		case AV_AUDIO_PCM:
			if ((fd_audio=open("/dev/adec_pcm",
					   O_RDWR|O_NONBLOCK)) < 0)
				return -1;
			printf("opened /dev/adec_pcm\n");
			if (ioctl(fd_audio, AV_SET_AUD_SRC, 1) < 0)
				return -1;
			if (ioctl(fd_audio, AV_SET_AUD_STREAMTYPE, 0) < 0)
				return -1;
			if (ioctl(fd_audio, AV_SET_AUD_FORMAT, &mix) < 0)
				return -1;
			ioctl(fd_audio, AV_SET_AUD_BYPASS, 1);

			av_sync();

			if (ioctl(fd_audio, AV_SET_AUD_PLAY, 0) < 0)
				return -1;

			break;
		case AV_AUDIO_AC3:
			if ((fd_audio=open("/dev/adec_ac3",
				     O_RDWR|O_NONBLOCK)) < 0)
				return -1;
			printf("opened /dev/adec_ac3\n");
			if (ioctl(fd_audio, AV_SET_AUD_SRC, 1) < 0)
				return -1;
			if (ioctl(fd_audio, AV_SET_AUD_STREAMTYPE, 0) < 0)
				return -1;
			if (ioctl(fd_audio, AV_SET_AUD_CHANNEL, 0) < 0)
				return -1;
			if (ioctl(fd_audio, AV_SET_AUD_SYNC, 2) < 0)
				return -1;
			if (ioctl(fd_audio, AV_SET_AUD_BYPASS, 0) < 0) {
				fprintf(stderr, "audio passthrough failed!\n");
				audio_output = -1;
				return -1;
			}

			av_sync();

			if (ioctl(fd_audio, AV_SET_AUD_PLAY, 0) < 0)
			  return -1;

			break;
		default:
			return -1;
			break;
		}
	}

	audio_output = type;

	return 0;
}

/*
 * av_set_pcm_param() - change the pcm audio parameters
 *
 * Arguments:
 *	rate		- audio bitrate
 *	type		- type of audio
 *				0  - Ogg Vorbis
 *				1  - AC3, WAV
 *				2+ - ???
 *	channels	- channels (1 = mono, 2 = stereo)
 *	big_endian	- 1 = big endian, 0 = little endian
 *	bits		- 24, 16
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_set_pcm_param(unsigned long rate, int type, int channels,
		 int big_endian, int bits)
{
	int iloop;
	int mix[5];

	if (channels == 1)
		mix[0] = 1;
	else if (channels == 2)
		mix[0] = 0;
	else
		return -1;

	/* 0,1=24bit(24) , 2,3=16bit */
	if (bits == 16)
		mix[1] = 2;
	else if (bits == 24)
		mix[1] = 0;
	else
		return -1;

	mix[3] = type;

	if (big_endian == 1)
		mix[4] = 1;
	else if (big_endian == 0)
		mix[4] = 0;
	else
		return -1;

	/*
	 * if there is an exact match for the frequency, use it.
	 */
	for(iloop = 0;iloop<numfrequencies;iloop++)
	{
		if(rate == pcmfrequencies[iloop][1])
		{
			mix[2] = pcmfrequencies[iloop][0];
			printf("Using %iHz input frequency.\n",
			       pcmfrequencies[iloop][1]);
			break;
		}
	}

	if (iloop >= numfrequencies) {
		fprintf(stderr,
			"Can not find suitable output frequency for %ld\n",
			rate);
		return -1;
	}

	if (ioctl(fd_audio, AV_SET_AUD_FORMAT, &mix) < 0)
		return -1;

	av_sync();

	if (ioctl(fd_audio, AV_SET_AUD_PLAY, 0) < 0)
		return -1;

	return 0;
}
