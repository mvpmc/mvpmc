/*
 *  Copyright (C) 2004, Jon Gettler
 *  http://mvpmc.sourceforge.net/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <mvp_widget.h>
#include <mvp_av.h>

#include "mvpmc.h"

static int fd = -1;

#define BSIZE	(1024*32)

void audio_play(mvp_widget_t *widget);

static void
audio_player(int reset)
{
	static char buf[BSIZE];
	static int n = 0, nput = 0, afd = 0;
	int tot, len;

	if (reset) {
		n = 0;
		nput = 0;
	}

	if (afd == 0)
		afd = av_audio_fd();

	len = read(fd, buf+n, BSIZE-n);
	n += len;

	if ((tot=write(afd, buf+nput, n-nput)) == 0) {
		mvpw_set_idle(NULL);
		mvpw_set_timer(root, audio_play, 100);
		return;
	}

	nput += tot;

	if (nput == n) {
		n = 0;
		nput = 0;
	}
}

static void
audio_idle(void)
{
	int reset = 0;

	if (fd == -1) {
		if ((fd=open(current, O_RDONLY|O_LARGEFILE|O_NDELAY)) < 0)
			return;
		av_play();

		av_set_audio_type(0);

		reset = 1;
	}

	audio_player(reset);
}

void
audio_play(mvp_widget_t *widget)
{
	mvpw_set_idle(audio_idle);
	mvpw_set_timer(root, NULL, 0);
}

void
audio_clear(void)
{
	fd = -1;
	av_reset();
}
