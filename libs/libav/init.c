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

#include "mvp_av.h"
#include "stb.h"

static int fd_video = -1;
static int fd_audio = -1;
static int pal_mode, aspect;
static int paused = 0;

static int
init_mtd1(void)
{
	int fd;
	short *mtd;

	if ((fd=open("/dev/mtd1", O_RDONLY)) < 0)
		return -1;

	if ((mtd=malloc(8192)) == NULL)
		return -1;
	read(fd, mtd, 8192);

	close(fd);

	pal_mode = mtd[2119];
	aspect = mtd[2125];

	free(mtd);

	return 0;
}

static int
av_sync(void)
{
	if (ioctl(fd_video, AV_SET_VID_SYNC, 2) != 0)
		return -1;
	if (ioctl(fd_audio, AV_SET_AUD_SYNC, 2) != 0)
		return -1;

	return 0;
}

static int
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

	if (ioctl(fd_video, AV_SET_VID_DISP_FMT, pal_mode) != 0)
		return -1;
	if (ioctl(fd_video, AV_SET_VID_SRC, 1) != 0)
		return -1;
	if (ioctl(fd_video, AV_SET_VID_OUTPUT, scart) != 0)
		return -1;

	return 0;
}
int
av_init(int letterbox)
{
	int video_mode = 0, audio_mode = 2;

	if (letterbox)
		video_mode = 1;

	if (init_mtd1() < 0)
		return -1;

	if ((fd_video=open("/dev/vdec_dev", O_RDWR|O_NONBLOCK)) < 0)
		return -1;
	if ((fd_audio=open("/dev/adec_mpg", O_RDWR|O_NONBLOCK)) < 0)
		return -1;

	if (set_output_method() < 0)
		return -1;

	if (ioctl(fd_video, AV_SET_VID_MODE, video_mode) < 0)
		return -1;
	if (ioctl(fd_video, AV_SET_VID_RATIO, aspect) < 0)
		return -1;

	if (ioctl(fd_audio, AV_SET_AUD_SRC, 1) < 0)
		return -1;
	if (ioctl(fd_audio, AV_SET_AUD_STREAMTYPE, audio_mode) < 0)
		return -1;
	if (ioctl(fd_audio, AV_SET_AUD_CHANNEL, 0) < 0)
		return -1;

	av_sync();

	if (ioctl(fd_audio, AV_SET_AUD_PLAY, 0) != 0)
		return -1;

	return 0;
}

int
av_set_audio_type(int audio_mode)
{
	return ioctl(fd_audio, AV_SET_AUD_STREAMTYPE, audio_mode);
}

int
av_write_audio(char *buf, int len)
{
	return write(fd_audio, buf, len);
}

int
av_write_video(char *buf, int len)
{
	return write(fd_video, buf, len);
}

int
av_attach_fb(void)
{
	if (ioctl(fd_video, AV_SET_VID_FB, 0) != 0)
		return -1;

	return 0;
}

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

	return 0;
}

int
av_audio_fd(void)
{
	return fd_audio;
}

int
av_video_fd(void)
{
	return fd_video;
}

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
	} else {
		if (ioctl(fd_audio, AV_SET_AUD_MUTE, 1) < 0)
			return -1;
		if (ioctl(fd_audio, AV_SET_AUD_PAUSE, 1) < 0)
			return -1;
		if (ioctl(fd_video, AV_SET_VID_PAUSE, 0) < 0)
			return -1;
		paused = 1;
	}

	return 0;
}

int
av_stop(void)
{
	if (ioctl(fd_audio, AV_SET_AUD_RESET, 0x11) < 0)
		return -1;
	if (ioctl(fd_video, AV_SET_VID_RESET, 0x11) < 0)
		return -1;

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
av_move(int x, int y, int video_mode)
{
	vid_pos_regs_t pos_d;

	memset(&pos_d, 0, sizeof(pos_d));

	pos_d.y = y;
	pos_d.x = x;

	ioctl(fd_video, AV_SET_VID_POSITION, &pos_d);
	ioctl(fd_video, AV_SET_VID_MODE, video_mode);

	if (video_mode == 0)
		ioctl(fd_video, AV_SET_VID_SRC, 1);

	return 0;
}
