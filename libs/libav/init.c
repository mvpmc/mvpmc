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
#include "av_local.h"

int fd_video = -1;
int fd_audio = -1;
int paused = 0;
int muted = 0;
int ffwd = 0;
av_mode_t vid_mode;
av_aspect_t aspect;

/*
 * av_init() - initialze the audio/video devices
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0 if the initialization succeeded, -1 if it failed
 */
int
av_init(void)
{
	int video_mode = 0, audio_mode = 2;

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

/*
 * av_init() - initialze the audio/video devices
 *
 * Arguments:
 *	on	- 1 to turn the LED on, 0 to turn it off
 *
 * Returns:
 *	0 if the LED toggle succeeded, -1 if it failed
 */
int
av_set_led(int on)
{
	static int rawir = -1;
	static int lit = 1;

	if (rawir < 0) {
		/*
		 * XXX: The first open doesn't seem to work...
		 */
		if ((rawir=open("/dev/rawir", O_RDONLY)) < 0)
			return -1;
		close(rawir);
		if ((rawir=open("/dev/rawir", O_RDONLY)) < 0)
			return -1;
	}

	if (lit == on)
		return 0;

	if (ioctl(rawir, IR_SET_LED, lit) < 0)
		return -1;

	lit = !lit;

	return 0;
}
