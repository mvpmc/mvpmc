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

#include "mvp_osd.h"

osd_surface_t *surface = NULL;
osd_surface_t *surface2 = NULL;

int
main(int argc, char **argv)
{
	int i;

	if ((surface=osd_create_surface(720, 576)) == NULL)
		return -1;
	if ((surface2=osd_create_surface(300, 200)) == NULL)
		return -1;

	osd_fill_rect(surface, 100, 100, 200, 200, osd_rgba(255, 0, 0, 255));
	osd_fill_rect(surface, 140, 140, 200, 200, osd_rgba(0, 255, 0, 255));
	osd_fill_rect(surface, 180, 180, 200, 200, osd_rgba(0, 0, 255, 255));

	osd_fill_rect(surface, 400, 100, 50, 50, osd_rgba(0, 255, 255, 255));
	osd_fill_rect(surface, 450, 100, 50, 50, osd_rgba(255, 255, 0, 255));
	osd_fill_rect(surface, 500, 100, 50, 50, osd_rgba(255, 0, 255, 255));

	osd_fill_rect(surface, 400, 150, 50, 50, osd_rgba(255, 110, 0, 255));
	osd_fill_rect(surface, 450, 150, 50, 50, osd_rgba(255, 165, 0, 255));
	osd_fill_rect(surface, 500, 150, 50, 50, osd_rgba(0, 255, 105, 255));

	osd_fill_rect(surface, 400, 200, 50, 50, osd_rgba(255, 150, 190, 255));
	osd_fill_rect(surface, 450, 200, 50, 50, osd_rgba(128, 128, 128, 255));
	osd_fill_rect(surface, 500, 200, 50, 50, osd_rgba(185, 255, 150, 255));

	osd_drawtext(surface, 250, 250, "Hello World!",
		     osd_rgba(0, 0, 0, 255),
		     osd_rgba(0, 255, 255, 255), 1, NULL);
	osd_drawtext(surface, 250, 325, "Hello World!",
		     osd_rgba(255, 255, 255, 255),
		     osd_rgba(255, 110, 0, 255), 1, NULL);
	osd_drawtext(surface, 250, 400, "Hello World!",
		     osd_rgba(255, 0, 0, 255),
		     osd_rgba(255, 255, 255, 255), 1, NULL);

	osd_fill_rect(surface2, 0, 0, 100, 100, osd_rgba(0, 255, 0, 255));
	osd_display_surface(surface2);
	osd_blit(surface, 450, 350, surface2, 0, 0, 100, 100);
	sleep(1);
	osd_display_surface(surface);

	osd_fill_rect(surface, 550, 250, 50, 50, osd_rgba(185, 255, 150, 255));
	{
		unsigned int c;
		int x, y;

		for (x=550; x<600; x++)
			for (y=250; y<300; y++) {
				c = osd_read_pixel(surface, x, y);
				osd_draw_pixel(surface, x, y, c);
			}
	}

	for (i=0; i<720; i+=75) {
		char text[32];
		snprintf(text, sizeof(text), "%d", i);
		osd_draw_vert_line(surface, i, 0, 50,
				   osd_rgba(255, 0, 0, 255));
		osd_drawtext(surface, i, 50, text,
			     osd_rgba(0, 255, 0, 255),
			     0, 0, NULL);
	}

	for (i=0; i<576; i+=75) {
		char text[32];
		snprintf(text, sizeof(text), "%d", i);
		osd_draw_horz_line(surface, 0, 50, i,
				   osd_rgba(255, 0, 0, 255));
		osd_drawtext(surface, 50, i, text,
			     osd_rgba(0, 255, 0, 255),
			     0, 0, NULL);
	}

	for (i=0; ; i++) {
		char text[32];
		snprintf(text, sizeof(text), "%d", i);
		osd_drawtext(surface, 450, 300, text,
			     osd_rgba(255, 0, 255, 255),
			     osd_rgba(0, 0, 0, 255), 1, NULL);
		usleep(1000);
	}

	return 0;
}
