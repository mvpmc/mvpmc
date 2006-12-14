/*
 *  Copyright (C) 2006, Jon Gettler
 *  http://www.mvpmc.org/
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mvp_osd.h"
#include "mvp_av.h"

#define INCLUDE_LINUX_LOGO_DATA
#define __initdata
#include "splash.h"

int
main(int argc, char **argv)
{
	osd_surface_t *surface;
	osd_indexed_image_t image;
	int x, y;

	av_init();

	if ((surface=osd_create_surface(720, 480, 0x0, OSD_FB)) == NULL) {
		exit(1);
	}

	image.colors = LINUX_LOGO_COLORS;
	image.width = LOGO_W;
	image.height = LOGO_H;
	image.red = linux_logo_red;
	image.green = linux_logo_green;
	image.blue = linux_logo_blue;
	image.image = linux_logo;

	x = (720 - image.width) / 2;
	y = (480 - image.height) / 2;

	if (osd_draw_indexed_image(surface, &image, x, y) < 0) {
		exit(1);
	}

	return 0;
}
