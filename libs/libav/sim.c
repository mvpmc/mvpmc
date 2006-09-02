/*
 *  Copyright (C) 2004, 2005, Jon Gettler
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

#include "mvp_av.h"
#include "stb.h"
#include "av_local.h"

/*
 * These are stub functions for use outside of a MediaMVP.  It might be
 * interesting to open up something like mplayer and allow mvpmc to feed
 * it the demuxed data.
 *
 * For now, just return success for all the functions, and pass mvpmc
 * file descriptors for /dev/null.
 */

static int afd = -1, vfd = -1;

int
av_init(void)
{
#if 1
	if ((afd=open("/dev/null", O_WRONLY)) < 0)
		return -1;
	if ((vfd=open("/dev/null", O_WRONLY)) < 0)
		return -1;
#else
	if ((afd=open("audio.mp2", O_WRONLY|O_CREAT|O_TRUNC, 0644)) < 0)
		return -1;
	if ((vfd=open("video.mpg", O_WRONLY|O_CREAT|O_TRUNC, 0644)) < 0)
		return -1;
#endif

	return AV_DEMUX_OFF;
}

int
av_attach_fb(void)
{
	return 0;
}

int
av_play(void)
{
	return 0;
}

int
av_get_audio_fd(void)
{
	return afd;
}

int
av_get_video_fd(void)
{
	return vfd;
}

int
av_set_audio_type(int audio_mode)
{
	return 0;
}

int
av_stop(void)
{
	return 0;
}

int
av_pause(void)
{
	return 0;
}

int
av_move(int x, int y, int video_mode)
{
	return 0;
}

int
av_ffwd(void)
{
	return 0;
}

int
av_mute(void)
{
	return 0;
}

int
av_reset(void)
{
	return 0;
}

int
av_reset_stc(void)
{
	return 0;
}

int
av_get_video_sync(pts_sync_data_t *p)
{
	return 0;
}

int
av_get_audio_sync(pts_sync_data_t *p)
{
	return 0;
}

int
set_audio_stc(uint64_t stc)
{
	return 0;
}

int
av_current_stc(av_stc_t *stc)
{
	return 0;
}

av_mode_t
av_get_mode(void)
{
	return AV_MODE_NTSC;
}

av_video_output_t
av_get_output(void)
{
	return AV_OUTPUT_COMPOSITE;
}

int
av_get_state(av_state_t *state)
{
	return 0;
}

av_tv_aspect_t
av_get_tv_aspect(void)
{
	return AV_TV_ASPECT_4x3;
}

int
av_set_mode(av_mode_t mode)
{
	return 0;
}

int
av_set_output(av_video_output_t device)
{
	return 0;
}

int
av_set_tv_aspect(av_tv_aspect_t aspect)
{
	return 0;
}

int
av_set_led(int on)
{
	return !on;
}

int
av_set_pcm_param(unsigned long rate, int type, int mono, int endian, int bits)
{
	return 0;
}

int
av_set_audio_output(av_audio_output_t type)
{
	return 0;
}

av_wss_aspect_t
av_set_video_aspect(av_video_aspect_t wide, int afd)
{
    /*Should maybe actually deal with AFD and return a "correct" WSS number*/
	return 0;
}

av_video_aspect_t
av_get_video_aspect(void)
{
	return 0;
}

int
av_video_blank(void)
{
	return 0;
}

int
av_deactivate(void)
{
	return 0;
}

int
av_delay_video(int usec)
{
	return 0;
}

int
av_get_volume(void)
{
	return AV_VOLUME_MAX;
}

int
av_set_volume(int volume)
{
	if ((volume < AV_VOLUME_MIN) || (volume > AV_VOLUME_MAX))
		return -1;
	else
		return 0;
}

int 
kern_read(unsigned long memaddr, void *buffaddr, unsigned int size)
{
   return 0;
}

int 
kern_write(unsigned long memaddr, void *buffaddr, unsigned int size)
{
   return 0;
}

int 
dcr_read(unsigned long regaddr, unsigned int *data)
{
   return 0;
}

int 
dcr_write(unsigned long regaddr, unsigned int data)
{
   return 0;
}

int 
mvpstb_get_vid_stc(unsigned long long *vstc)
{
   return 0;
}

int 
mvpstb_get_vid_pts(unsigned long long *vpts)
{
   return 0;
}

int 
mvpstb_get_aud_stc(unsigned long long *astc)
{
   return 0;
}

int 
mvpstb_get_aud_pts(unsigned long long *apts)
{
   return 0;
}

int
mvpstb_set_video_sync(int on)
{
   return 0;
}

int
mvpstb_set_audio_sync(int on)
{
   return 0;
}

int 
mvpmod_start_audit(unsigned long interval_ms)
{
   return 0;
}

int 
mvpmod_stop_audit(void)
{
   return 0;
}

int
av_colorbars(int on)
{
	return 0;
}

int
av_empty(void)
{
	return 1;
}
