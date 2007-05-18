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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "mvp_av.h"
#include "av_local.h"

/*
 * init_mtd() - Read configuration data out of flash
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0 if mtd partition could be read, -1 if it failed
 */
int
init_mtd(void)
{
	int fd, i = 0, found = 0;
	short *mtd;
	char path[64];
	av_mode_t video = AV_MODE_NTSC;
	av_video_aspect_t ratio = AV_TV_ASPECT_4x3;
	short combo;

	if ((fd=open("/proc/mtd", O_RDONLY)) > 0) {
		FILE *f = fdopen(fd, "r");
		char line[64];
		/* read the header */
		fgets(line, sizeof(line), f);
		/* read each mtd entry */
		while (fgets(line, sizeof(line), f) != NULL) {
			if (strstr(line, " VPD") != NULL) {
				found = 1;
				break;
			}
			i++;
		}
		fclose(f);
		close(fd);
	}

	if (!found) {
		return -1;
	}

	snprintf(path, sizeof(path), "/dev/mtd%d", i);

	if ((fd=open(path, O_RDONLY)) < 0)
		return -1;

	if ((mtd=malloc(8192)) == NULL) {
		close(fd);
		return -1;
	}

	if (read(fd, mtd, 8192) != 8192) {
		close(fd);
		return -1;
	}

	close(fd);

	video = mtd[2119];
	combo = mtd[2124];
	flicker = combo & 0xff;
	ratio = mtd[2125] & 0xff;

	free(mtd);

	/* verify video mode is reasonable */
	switch (video) {
	case AV_MODE_NTSC:
	case AV_MODE_PAL:
		break;
	default:
		return -1;
	}

	/* verify aspect ratio is reasonable */
	switch (ratio) {
	case AV_TV_ASPECT_4x3:
	case AV_TV_ASPECT_16x9:
		break;
	default:
		return -1;
	}

	vid_mode = video;
	tv_aspect = ratio;

	return 0;
}
