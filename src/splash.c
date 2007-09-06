/*
 *  Copyright (C) 2006,2007, Jon Gettler
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
#include <getopt.h>
#include <string.h>

#include "mvp_osd.h"
#include "mvp_av.h"

#if defined(STANDALONE)
#define INCLUDE_LINUX_LOGO_DATA
#define __initdata
#define splash_main	main
#endif

#include "splash.h"

static struct option opts[] = {
	{ "blank", no_argument, 0, 'b' },
	{ "logo", no_argument, 0, 'l' },
	{ "progress", required_argument, 0, 'p' },
	{ "state", required_argument, 0, 's' },
	{ 0, 0, 0, 0 }
};

static int width, height;

static int
blank(osd_surface_t *surface)
{
	unsigned int c;

	c = osd_rgba(0xff, 0x0, 0, 0xff);

	osd_fill_rect(surface, 0, 0, width, height, c);

	return 0;
}

#if defined(STANDALONE)
static int
draw_logo(osd_surface_t *surface)
{
	osd_indexed_image_t image;
	int x, y;

	osd_fill_rect(surface, 0, 0, width, height, 0);

	image.colors = LINUX_LOGO_COLORS;
	image.width = LOGO_W;
	image.height = LOGO_H;
	image.red = linux_logo_red;
	image.green = linux_logo_green;
	image.blue = linux_logo_blue;
	image.image = linux_logo;

	x = (width - image.width) / 2;
	y = (height - image.height) / 2;

	if (osd_draw_indexed_image(surface, &image, x, y) < 0) {
		return -1;
	}

	return 0;
}
#endif /* STANDALONE */

static int
draw_progress(osd_surface_t *surface, int location, int state)
{
	int x, y;
	unsigned int c, red, green, white, blue;

	red = osd_rgba(0xff, 0, 0, 0xff);
	green = osd_rgba(0, 0xff, 0, 0xff);
	white = osd_rgba(0xff, 0xff, 0xff, 0xff);
	blue = osd_rgba(0, 0, 0xff, 0xff);

	osd_palette_add_color(surface, red);
	osd_palette_add_color(surface, green);
	osd_palette_add_color(surface, blue);
	osd_palette_add_color(surface, white);

	switch (state) {
	case 0:
		c = red;
		break;
	case 1:
		c = green;
		break;
	case 2:
		c = white;
		break;
	case 3:
		c = blue;
		break;
	default:
		return -1;
	}

	y = (height / 2) + 75;
	x = (width / 2) - 150 + (location * 40);

	osd_fill_rect(surface, x, y, 20, 20, c);

	return 0;
}

int
splash_main(int argc, char **argv)
{
	osd_surface_t *surface;
	int ret = -1;
	int c, opt_index;
	char *opt_p = NULL, *opt_s = NULL;

	av_init();

	if (av_get_mode() == AV_MODE_PAL) {
		width = 720;
		height = 576;
		av_set_mode(AV_MODE_PAL);
	} else {
		width = 720;
		height = 480;
	}

	if ((surface=osd_create_surface(width, height, 0, OSD_FB)) == NULL) {
		goto err;
	}

	while ((c=getopt_long(argc, argv,
			      "blp:s:", opts, &opt_index)) != -1) {
		switch (c) {
		case 'b':
			ret = blank(surface);
			break;
#if defined(STANDALONE)
		case 'l':
			ret = draw_logo(surface);
			break;
#endif /* STANDALONE */
		case 'p':
			opt_p = strdup(optarg);
			break;
		case 's':
			opt_s = strdup(optarg);
			break;
		}
	}

	if (opt_p && opt_s) {
		ret = draw_progress(surface, atoi(opt_p), atoi(opt_s));
	}

 err:
	return ret;
}
