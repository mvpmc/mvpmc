/*
 *  Copyright (C) 2004, 2005, 2006, Jon Gettler
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

static av_video_aspect_t vid_aspect = 0;
static vid_disp_mode_t vid_dispmode = 0;/*As set from UI*/
static vid_disp_mode_t cur_dispmode = 0;/*Including automatic zooming from AFD*/
static int in_thumbnail = 0; /* Is video currently thumbnailed? */
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

	usleep(1000);

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
 * av_set_output() - set the video output device
 *
 * Arguments:
 *	device	- AV_OUTPUT_SVIDEO or AV_OUTPUT_COMPOSITE
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_set_output(av_video_output_t device)
{
	if (ioctl(fd_video, AV_SET_VID_OUTPUT, device) != 0)
		return -1;
	output = device;

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
av_video_output_t
av_get_output(void)
{
	return output;
}

int
av_init_letterbox(void)
{
    int height,y;
    if(av_get_mode() == AV_MODE_PAL)
	height = 576;
    else
	height = 480;
    y = (((height*4)/16))/2;
    /* STB seems to want offset/field, rather than offset/frame */
    y /=2;
    return mvpstb_set_lbox_offset(y);
}


/*
 * av_set_video_aspect() - set the aspect ratio of the video being played
 *
 * Arguments:
 *	wide	- AV_ASPECT_4x3 or AV_ASPECT_16x9
 *	afd	- AFD value (if present)
 *
 * Returns:
 *	new WSS signal calculated from aspect and AFD
 */
av_wss_aspect_t
av_set_video_aspect(av_video_aspect_t vid_ar, int afd)
{
    /* Work out what WSS signal to present, and whether to zoom, based upon
     * AFD and MPEG aspect following specification at 
     * http://www.dtg.org.uk/publications/books/afd.pdf
     */
    av_wss_aspect_t new_wss = WSS_ASPECT_UNKNOWN;
    int zoom_type = 0;
    vid_aspect = vid_ar;

    switch(vid_ar)
    {
	case AV_VIDEO_ASPECT_4x3:
	    switch(afd)
	    {
		case 0xA:
		    /* 16x9 letterboxed in a 4x3 raster */
		    new_wss = WSS_ASPECT_BOX_16x9_CENTRE;
		    /* TODO: If we're writing to a 16x9 display, it may be
		     * better to "zoom" in the MVP (and return a different
		     * WSS signal)if it's ever determined how that could
		     * be done.
		     */
		    break;
		case 0xB:
		    /* 14x9 letterboxed in a 4x3 raster */
		    new_wss = WSS_ASPECT_BOX_14x9_CENTRE;
		    break;
		case 0xD:
		    /* 4x3 full frame, shot to protect 14x9 area */
		    new_wss = WSS_ASPECT_FULL_4x3_PROTECT_14x9;
		    break;
		case 0xE:
		    /* 16x9 letterboxed image in a 4x3 raster, original
		     * material shot to protect 14x9 area
		     */

		    /*TODO: It is preferred that for a 4x3 display we zoom
		     * slightly to show a 14x9 letterboxed in a 4x3 raster.
		     * If we ever know how to zoom we should do this.
		     */
		    new_wss=WSS_ASPECT_BOX_16x9_CENTRE;
		    break;
		case 0xF:
		    /* 16x9 letterboxed image in a 4x3 raster, original
		     * material shot to protect 4x3 area.
		     */
		    /*TODO: It is preferred that for a 4x3 display we zoom
		     * slightly to show a 4x3 full frame picture.
		     * If we ever know how to zoom we should do this.
		     */
		    new_wss = WSS_ASPECT_BOX_16x9_CENTRE;
		    break;
		case 0x8:
		case 0x9:
		default:
		    /* 4x3 Full Frame */
		    new_wss = WSS_ASPECT_FULL_4x3;
		    break;
	    }
	    break;
	case AV_VIDEO_ASPECT_16x9:
	    switch(afd)
	    {
		default:
		case 0x8:
		    /*16:9 full frame in a 16:9 raster */
		    if(tv_aspect == AV_TV_ASPECT_4x3)
		    {
			new_wss = WSS_ASPECT_BOX_16x9_CENTRE;
		    }
		    else if(tv_aspect == AV_TV_ASPECT_4x3_CCO)
		    {
			new_wss = WSS_ASPECT_FULL_4x3;
		    }
		    else 
			new_wss = WSS_ASPECT_FULL_16x9;
		    break;
		case 0x9:
		    /*4:3 pillarboxed in a 16:9 raster */
		    /* According to the spec we should crop this even
		     * for a 16x9 display, however the only way to do this
		     * is to switch the display type, which we currently
		     * can't do without restarting the whole of mvpmc
		     */
		    if(IS_4x3(tv_aspect))
		    {
			zoom_type = 1;
			new_wss = WSS_ASPECT_FULL_4x3;
		    }
		    else
		    {
			/* There isn't actually a way to signal that this is
			 * 4x3 in a 16:9 raster using WSS, so just lie
			 * and tell the telly it's 16x9, full
			 */
			new_wss = WSS_ASPECT_FULL_16x9;
		    }
		    break;
		case 0xA:
		    /*16:9 full frame image, in 16:9 frame, no protected area
		     *The specification says we should force letterboxing on
		     *4:3 displays
		     */
		    if(IS_4x3(tv_aspect))
		    {
			zoom_type = 2;
			new_wss = WSS_ASPECT_BOX_16x9_CENTRE;
		    }
		    else
		    {
			new_wss = WSS_ASPECT_FULL_16x9;
		    }
		    break;
		case 0xB:
		    /*14:9 pillarboxed in a 16:9 raster*/
		    /*TODO: zoom to 14:9 letterbox if we're on a 4:3 display
		     * if we ever work out arbitrary zooming
		     */
		    if(IS_4x3(tv_aspect))
		    {
			/*Spec says that since we can't scale to a 14:9 
			 * letterbox we should go for CCO
			 */
			zoom_type = 1;
			new_wss = WSS_ASPECT_FULL_4x3;
		    }
		    else
		    {
			new_wss = WSS_ASPECT_FULL_16x9;
		    }
		    break;
		case 0xD:
		    /* 4:3 pillarboxed image 14:9 protected in 16:9 raster*/
		    if(IS_16x9(tv_aspect))
		    {
			/* If we could do a CCO whilst in 16x9 mode we'd
			 * go for this one:
			new_wss = WSS_ASPECT_FULL_4x3_PROTECT_14x9;
			 * but since we can't, then we'll just have to leave
			 * as-is:
			 */
			new_wss = WSS_ASPECT_FULL_16x9;
		    }
		    else
		    {
			/*Force CCO mode:*/
			zoom_type = 1;
			new_wss = WSS_ASPECT_FULL_4x3;
		    }
		    break;
		case 0xE:
		    /*16:9 full frame image, 14:9 protected in 16:9 raster*/
		    /*TODO: 14:9 LB zoom on 4:3 displays if we can */
		    if(tv_aspect == AV_TV_ASPECT_4x3)
			new_wss = WSS_ASPECT_BOX_16x9_CENTRE;
		    else if(tv_aspect == AV_TV_ASPECT_4x3_CCO)
			new_wss = WSS_ASPECT_FULL_4x3;
		    else
			new_wss = WSS_ASPECT_FULL_16x9;
		    break;
		case 0xF:
		    /* 16:9 full frame image, 4:3 protected in 16:9 raster*/
		    if(tv_aspect == AV_TV_ASPECT_4x3)
			new_wss = WSS_ASPECT_BOX_16x9_CENTRE;
		    else if(tv_aspect == AV_TV_ASPECT_4x3_CCO)
			new_wss = WSS_ASPECT_FULL_4x3;
		    else
			new_wss = WSS_ASPECT_FULL_16x9;
		    break;
	    }
	    break;
    }
    switch(zoom_type)
    {
	/*Force display mode to create required zoom:*/
	case 1:
	    cur_dispmode = VID_DISPMODE_NORM;/*Centre Cut Out*/
	    break;
	case 2:
	    cur_dispmode = VID_DISPMODE_LETTERBOX; /*Force letterbox */
	    break;
	default:
	    cur_dispmode = vid_dispmode;
	    break;
    }
    if (!in_thumbnail && ioctl(fd_video, AV_SET_VID_OUTPUT_MODE, cur_dispmode) < 0)
	return 0;
    return new_wss;
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
av_video_aspect_t
av_get_video_aspect(void)
{
	return vid_aspect;
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
av_set_tv_aspect(av_tv_aspect_t ratio)
{
    vid_disp_mode_t new_dispmode;
    av_tv_aspect_t new_display_aspect;
    av_init_letterbox();
    switch(ratio)
    {
	case AV_TV_ASPECT_16x9:
	    new_display_aspect = AV_TV_ASPECT_16x9;
	    new_dispmode = VID_DISPMODE_NORM;
	    break;
	case AV_TV_ASPECT_4x3_CCO:
	    new_dispmode = VID_DISPMODE_NORM;
	    new_display_aspect = AV_TV_ASPECT_4x3;
	    break;
	case AV_TV_ASPECT_4x3:
	    new_dispmode = VID_DISPMODE_LETTERBOX;
	    new_display_aspect = AV_TV_ASPECT_4x3;
	    break;
	default:
	    return -1;
    }
    if (ioctl(fd_video, AV_SET_VID_OUTPUT_RATIO, new_display_aspect) < 0)
	return -1;
    if (!in_thumbnail && ioctl(fd_video, AV_SET_VID_OUTPUT_MODE, new_dispmode) < 0)
	return -1;
    tv_aspect = ratio;
    cur_dispmode = vid_dispmode = new_dispmode;
    return 0;
}

/*
 * av_get_tv_aspect() - get the aspect ratio of the output device (ie, the TV)
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	AV_TV_ASPECT_4x3 or AV_TV_ASPECT_4x3_CCO or AV_TV_ASPECT_16x9
 */
av_tv_aspect_t
av_get_tv_aspect(void)
{
	return tv_aspect;
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
	if (!state.mute) {
		if (ioctl(fd_audio, AV_SET_AUD_MUTE, 0) < 0)
			return -1;
	}

	av_sync();
	if (ioctl(fd_video, AV_SET_VID_PLAY, 0) != 0)
		return -1;
	av_sync();
	if (ioctl(fd_audio, AV_SET_AUD_PLAY, 0) != 0)
		return -1;

	state.pause = 0;
	state.ffwd = 0;

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
	int ret;

	if (state.pause) {
		if (ioctl(fd_audio, AV_SET_AUD_UNPAUSE, 1) < 0)
			return -1;
		if (!state.mute) {
			if (ioctl(fd_audio, AV_SET_AUD_MUTE, 0) < 0)
				return -1;
		}
		av_attach_fb();
		av_play();
		av_sync();
		state.pause = false;
		ret = 0;
	} else {
		if (ioctl(fd_audio, AV_SET_AUD_MUTE, 1) < 0)
			return -1;
		if (ioctl(fd_audio, AV_SET_AUD_PAUSE, 1) < 0)
			return -1;
		if (ioctl(fd_video, AV_SET_VID_PAUSE, 0) < 0)
			return -1;
		state.pause = true;
		if (state.ffwd) {
			state.ffwd = false;
		}
		ret = 1;
	}

	return ret;
}

int
av_pause_video()
{
	if (state.pause)
		return -1;

	if (ioctl(fd_video, AV_SET_VID_PAUSE, 0) < 0)
		return -1;

	return 0;
}

int
av_delay_video(int usec)
{
        if (av_pause_video() < 0)
		return -1;

	usleep(usec);
	av_play();

	return 0;
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
	if (state.mute) {
		if (ioctl(fd_audio, AV_SET_AUD_MUTE, 0) < 0)
			return -1;
		state.mute = false;
	} else {
		if (ioctl(fd_audio, AV_SET_AUD_MUTE, 1) < 0)
			return -1;
		state.mute = true;
	}

	return state.mute;
}

int
av_set_mute(bool mute)
{
	if (mute == state.mute)
		return 0;

	if (av_mute() < 0)
		return -1;

	return 0;
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
	int ffwd;

	if (state.ffwd) {
		state.ffwd = false;
		ffwd = 0;
	} else {
		state.ffwd = true;
		ffwd = 1;
	}

	if (ioctl(fd_video, AV_SET_VID_FFWD, ffwd) < 0)
		return -1;

	if (state.ffwd == false) {
		if (ioctl(fd_video, AV_SET_VID_PLAY, 0) != 0)
			return -1;
		if (state.mute == false) {
			if (ioctl(fd_audio, AV_SET_AUD_MUTE, 1) < 0)
				return -1;
		}
		av_sync();
	} else {
		if (ioctl(fd_audio, AV_SET_AUD_MUTE, 1) < 0)
			return -1;
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

	if (state.mute)
		if (ioctl(fd_audio, AV_SET_AUD_MUTE, 0) < 0)
			return -1;

	if (ioctl(fd_audio, AV_SET_AUD_PLAY, 0) != 0)
		return -1;
	av_sync();

	state.pause = false;
	state.mute = false;
	state.ffwd = false;

	return 0;
}

/*
 * av_reset_stc() - reset audio/video STC to 0
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_reset_stc(void)
{
	av_set_audio_stc(0);
	av_set_video_stc(0);

	return 0;
}

/*
 * av_get_audio_sync() - get the audio sync data
 *
 * Arguments:
 *	p	- PTS audio sync pointer
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_get_audio_sync(pts_sync_data_t *p)
{
	int ret;

	if ((ret=ioctl(fd_audio, AV_GET_AUD_SYNC, p)) == 0) {
		p->stc = (p->stc >> 31 ) | (p->stc & 1);
		p->pts = (p->pts >> 31 ) | (p->pts & 1);
	}

	return ret;
}

/*
 * set_audio_stc() - set the audio STC
 *
 * Arguments:
 *	stc	- new audio STC
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_set_audio_stc(uint64_t stc)
{
	return ioctl(fd_audio, AV_SET_AUD_STC, &stc);
}

/*
 * av_get_video_sync() - get the video sync data
 *
 * Arguments:
 *	p	- PTS video sync pointer
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_get_video_sync(pts_sync_data_t *p)
{
	int ret;

	if ((ret=ioctl(fd_video, AV_GET_VID_SYNC, p)) == 0) {
		p->stc = (p->stc >> 31 ) | (p->stc & 1);
		p->pts = (p->pts >> 31 ) | (p->pts & 1);
	}

	return ret;
}

/*
 * av_set_video_stc() - set the video STC
 *
 * Arguments:
 *	stc	- new video STC
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_set_video_stc(uint64_t stc)
{
	return ioctl(fd_video, AV_SET_VID_STC, &stc);
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

	if (av_get_video_sync(&pts) == 0) {
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
	{
	    	in_thumbnail = 0;
		ioctl(fd_video, AV_SET_VID_OUTPUT_MODE, cur_dispmode);
	}
	else
	{
	        in_thumbnail = 1;
		ioctl(fd_video, AV_SET_VID_OUTPUT_MODE, video_mode);
	}

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
		fd_audio=-1;

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
		case AV_AUDIO_CLOSE:
			printf("closed audio device\n");
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
 *	big_endian	- true or false
 *	bits		- 24, 16
 *
 * Returns:
 *	0 if it succeeded, -1 if it failed
 */
int
av_set_pcm_param(unsigned long rate, int type, int channels,
		 bool big_endian, int bits)
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

	if (big_endian)
		mix[4] = 1;
	else
		mix[4] = 0;

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

	if (ioctl(fd_audio, AV_SET_AUD_FORMAT, &mix) < 0) {
		fprintf(stderr,"Can not set audio format %d %d %d %d%d\n",
			mix[0],mix[1],mix[2],mix[3],mix[4]);
		return -1;
	}

	av_sync();

	if (ioctl(fd_audio, AV_SET_AUD_PLAY, 0) < 0){
		fprintf(stderr,
			"Can not set audio play");
		return -1;
	}

	return 0;
}

int
av_get_state(av_state_t *s)
{
	if (s == NULL)
		return -1;

	s->mute = state.mute;
	s->pause = state.pause;
	s->ffwd = state.ffwd;

	return 0;
}

int
av_deactivate(void)
{
	ioctl(fd_video, AV_SET_VID_DENC, 0);

	return 0;
}

int
av_get_volume(void)
{
	unsigned int volume;

	if (ioctl(fd_audio, AV_GET_AUD_VOLUME, &volume) < 0)
		return -1;

	volume = volume & 0xff;

	return AV_VOLUME_MAX - (int)volume;
}

int
av_set_volume(int volume)
{
	unsigned long vol, v;

	if ((volume < AV_VOLUME_MIN) || (volume > AV_VOLUME_MAX))
		return -1;

	v = AV_VOLUME_MAX - volume;

	vol = (v << 24) | (v << 16) | (v << 8) | v;

	if (ioctl(fd_audio, AV_SET_AUD_VOLUME, &vol) < 0)
		return -1;

	return 0;
}

int
av_colorbars(bool on)
{
	int val;

	if (on)
		val = 1;
	else
		val = 0;

	return ioctl(fd_video, AV_SET_VID_COLORBAR, val);
}

int
av_empty(void)
{
	int a, v;

	a = mvpstb_audio_end();
	v = mvpstb_video_end();

	if ((a == -1) || (v == -1))
		return -1;

	return a && v;
}

/*
 * av_get_flicker() - get the flicker mode
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0-3
 */
int
av_get_flicker(void)
{
	return flicker;
}
