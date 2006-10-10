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
#include <sys/time.h>
#include <time.h>
#include <libgen.h>

#include "mvp_osd.h"

#if !defined(__powerpc__)
#error unsupported architecture
#endif

#define OSD_COLOR(r,g,b,a)	((a<<24) | (r<<16) | (g<<8) | b)

#define OSD_WHITE	OSD_COLOR(255,255,255,255)
#define OSD_RED		OSD_COLOR(255,0,0,255)
#define OSD_BLUE	OSD_COLOR(0,0,255,255)
#define OSD_GREEN	OSD_COLOR(0,255,0,255)
#define OSD_BLACK	OSD_COLOR(0,0,0,255)

static int width = 720, height = 480;

static unsigned long lr;

static struct timeval start, end, delta;

#define timer_start()	fflush(stdout); gettimeofday(&start, NULL)

#define timer_end()	gettimeofday(&end, NULL)

#define timer_print()	timersub(&end,&start,&delta); \
			snprintf(buf, sizeof(buf), "%5.2f seconds\n", \
			         delta.tv_sec + (delta.tv_usec/1000000.0)); \
			printf(buf)

#define FAIL		{ \
				__dummy_func(); \
				asm ("mflr %0" : "=r" (lr)); \
				goto err; \
			}

void
__dummy_func(void)
{
}

static int
test_sanity(char *name)
{
	osd_surface_t *surface = NULL;
	unsigned long c1, c2, c3;

	printf("testing osd sanity\t");

	timer_start();

	if ((surface=osd_create_surface(width, height,
					0, OSD_DRAWING)) == NULL)
		FAIL;

	c1 = rand() | 0xff000000;
	c2 = rand() | 0xff000000;
	c3 = rand() | 0xff000000;

	if (osd_drawtext(surface, 0, 0, "Hello World!", c1, c2, c3, NULL) < 0)
		FAIL;

	timer_end();

	return 0;

 err:
	return -1;
}

static int
test_create_surfaces(char *name)
{
	int i;
	int n = 50;
	osd_surface_t *surface = NULL;

	printf("creating %d surfaces\t", n);

	timer_start();

	for (i=0; i<n; i++) {
		if ((surface=osd_create_surface(width, height,
						0, OSD_DRAWING)) == NULL)
			FAIL;
		if (i == 0) {
			if (osd_display_surface(surface) < 0)
				FAIL;
			if (osd_drawtext(surface, 100, 200,
					 "Creating surfaces!",
					 OSD_GREEN, OSD_BLACK, OSD_BLACK,
					 NULL) < 0)
				FAIL;
		} else {
			if (osd_destroy_surface(surface) < 0)
				FAIL;
		}
	}

	timer_end();

	return 0;

 err:
	return -1;
}

static int
test_text(char *name)
{
	int i;
	osd_surface_t *surface = NULL;

	printf("testing %s\t\t", name);

	timer_start();

	if ((surface=osd_create_surface(width, height,
					0, OSD_DRAWING)) == NULL)
		FAIL;

	for (i=0; i<height; i+=50) {
		unsigned long c1, c2, c3;

		c1 = rand() | 0xff000000;
		c2 = rand() | 0xff000000;
		c3 = rand() | 0xff000000;

		if (osd_drawtext(surface, i, i, "Hello World!",
				 c1, c2, c3, NULL) < 0)
			FAIL;
	}

	if (osd_display_surface(surface) < 0)
		FAIL;

	timer_end();

	return 0;

 err:
	return -1;
}

static int
test_rectangles(char *name)
{
	int i;
	int n = 500;
	osd_surface_t *surface = NULL;

	printf("drawing %d rectangles\t", n);

	timer_start();

	if ((surface=osd_create_surface(width, height,
					0, OSD_DRAWING)) == NULL)
		FAIL;

	if (osd_display_surface(surface) < 0)
		FAIL;

	for (i=0; i<n; i++) {
		int x, y, w, h;
		unsigned long c;

		x = rand() % width;
		y = rand() % height;
		w = rand() % (width - x);
		h = rand() % (height - y);
		c = rand() | 0xff000000;

		if (osd_fill_rect(surface, x, y, w, h, c) < 0)
			FAIL;
	}

	timer_end();

	return 0;

 err:
	return -1;
}

static int
test_circles(char *name)
{
	int i;
	int n = 30;
	osd_surface_t *surface = NULL;

	printf("drawing %d circles\t", n);

	timer_start();

	if ((surface=osd_create_surface(width, height,
					0, OSD_DRAWING)) == NULL)
		FAIL;

	if (osd_display_surface(surface) < 0)
		FAIL;

	for (i=0; i<n; i++) {
		int x, y, r, f;
		unsigned long c;

		x = rand() % width;
		y = rand() % height;
		r = rand() % 100;
		c = rand() | 0xff000000;
		f = rand() % 2;

		if (osd_draw_circle(surface, x, y, r, f, c) < 0)
			FAIL;
	}

	timer_end();

	return 0;

 err:
	return -1;
}

static int
test_polygons(char *name)
{
	int i;
	int n = 100;
	osd_surface_t *surface = NULL;

	printf("drawing %d polygons\t", n);

	timer_start();

	if ((surface=osd_create_surface(width, height,
					0, OSD_DRAWING)) == NULL)
		FAIL;

	if (osd_display_surface(surface) < 0)
		FAIL;

	for (i=0; i<n; i++) {
		int x[12], y[12];
		int n, i;
		unsigned long c;

		n = (rand() % 8) + 3;

		for (i=0; i<n; i++) {
			x[i] = rand() % width;
			y[i] = rand() % height;
		}

		c = rand() | 0xff000000;

		if (osd_draw_polygon(surface, x, y, n, c) < 0)
			FAIL;
	}

	timer_end();

	return 0;

 err:
	return -1;
}

static int
test_lines(char *name)
{
	int i, j;
	int p = 10, n = 100;
	osd_surface_t *surface = NULL;

	printf("drawing %d lines\t", p*n);

	timer_start();

	if ((surface=osd_create_surface(width, height,
					0, OSD_DRAWING)) == NULL)
		FAIL;

	if (osd_display_surface(surface) < 0)
		FAIL;

	for (i=0; i<p; i++) {
		int x1, y1;
		unsigned long c;

		x1 = rand() % width;
		y1 = rand() % height;
		c = rand() | 0xff000000;

		for (j=0; j<n; j++) {
			int x2, y2;

			x2 = rand() % width;
			y2 = rand() % height;

			if (osd_draw_line(surface, x1, y1, x2, y2, c) < 0)
				FAIL;
		}
	}

	timer_end();

	return 0;

 err:
	return -1;
}

static int
test_display_control(char *name)
{
	osd_surface_t *surface = NULL;

	printf("testing display control\t");

	timer_start();

	if ((surface=osd_create_surface(width, height,
					OSD_BLUE, OSD_DRAWING)) == NULL)
		FAIL;

	if (osd_set_engine_mode(surface, 0) < 0)
		FAIL;

	if (osd_display_surface(surface) < 0)
		FAIL;

	if (osd_get_display_control(surface, 2) < 0)
		FAIL;
	if (osd_get_display_control(surface, 3) < 0)
		FAIL;
	if (osd_get_display_control(surface, 4) < 0)
		FAIL;
	if (osd_get_display_control(surface, 5) < 0)
		FAIL;
	if (osd_get_display_control(surface, 7) < 0)
		FAIL;
	if (osd_get_display_options(surface) < 0)
		FAIL;

	timer_end();

	return 0;

 err:
	return -1;
}

static int
test_blit(char *name)
{
	osd_surface_t *surface = NULL, *hidden = NULL;
	int i, j, k;
	int h, w;
	int mx, my;
	int x1, y1, x2, y2;
	int c;

	printf("testing blit\t\t");

	timer_start();

	if ((surface=osd_create_surface(width, height,
					0, OSD_DRAWING)) == NULL)
		FAIL;
	if ((hidden=osd_create_surface(width, height, 0, OSD_DRAWING)) == NULL)
		FAIL;

	if (osd_display_surface(surface) < 0)
		FAIL;

	w = width / 4;
	h = height / 4;

	for (i=0; i<4; i++) {
		x1 = i * w;
		x2 = (i+1) * w - 1;
		for (j=0; j<4; j++) {
			y1 = j * h;
			y2 = (j+1) * h - 1;
			mx = x1 + (w/2);
			my = y1 + (h/2);
			c = rand() | 0xff000000;
			for (k=0; k<300; k++) {
				int ex = x1 + (rand() % w);
				int ey = y1 + (rand() % h);
				if (osd_draw_line(hidden,
						  mx, my, ex, ey, c) < 0)
					FAIL;
			}
			if (osd_blit(surface, x1, y1, hidden, x1, y1, w, h) < 0)
				FAIL;
		}
	}

	timer_end();

	return 0;

 err:
	return -1;
}

static int
test_cursor(char *name)
{
	osd_surface_t *surface = NULL, *cursor = NULL;
	int i;
	int x, y;

	printf("testing cursor\t\t");

	timer_start();

	if ((surface=osd_create_surface(width, height,
					0, OSD_DRAWING)) == NULL)
		FAIL;

	if ((cursor=osd_create_surface(20, 20, 0, OSD_CURSOR)) == NULL)
		FAIL;

	for (x=0; x<20; x++) {
		for (y=0; y<20; y++) {
			if (osd_draw_pixel(cursor, x, y, OSD_RED) < 0)
				FAIL;
		}
	}

	if (osd_fill_rect(surface, 50, 50, 20, 20, OSD_RED) < 0)
		FAIL;

	for (y=100; y<300; y+=30) {
		if (osd_fill_rect(surface, 50, y, 400, 10, OSD_WHITE) < 0)
			FAIL;
	}

	if (osd_display_surface(surface) < 0)
		FAIL;

	if (osd_display_surface(cursor) < 0)
		FAIL;

	x = y = 0;
	for (i=0; i<100; i++) {
		x += 4;
		y += 4;
		if (osd_move_cursor(cursor, x, y) < 0)
			FAIL;
		usleep(10000);
	}

	timer_end();

	return 0;

 err:
	return -1;
}

typedef struct {
	char *name;
	int sleep;
	int (*func)(char*);
} tester_t;

static tester_t tests[] = {
	{ "sanity",		0,	test_sanity },
	{ "create surfaces",	0,	test_create_surfaces },
	{ "text",		2,	test_text },
	{ "rectangles",		2,	test_rectangles },
	{ "circles",		2,	test_circles },
	{ "lines",		2,	test_lines },
	{ "polygons",		2,	test_polygons },
	{ "display control",	2,	test_display_control },
	{ "blit",		2,	test_blit },
	{ "cursor",		2,	test_cursor },
	{ NULL, 0, NULL },
};

void
print_help(char *prog)
{
	printf("Usage: %s [-hl]\n", basename(prog));
	printf("\t-h        print help\n");
	printf("\t-l        list tests\n");
}

void
print_tests(void)
{
	int i = 0;

	while (tests[i].name != NULL) {
		printf("\t%s\n", tests[i++].name);
	}
}

int
main(int argc, char **argv)
{
	int i = 0;
	int c;
	char buf[256];
	osd_surface_t *surface;
	int ret = 0;

	while ((c=getopt(argc, argv, "hl")) != -1) {
		switch (c) {
		case 'h':
			print_help(argv[0]);
			exit(0);
			break;
		case 'l':
			print_tests();
			exit(0);
			break;
		default:
			print_help(argv[0]);
			exit(1);
			break;
		}
	}

	srand(getpid());

	while (tests[i].name) {
		if (tests[i].func(tests[i].name) == 0) {
			timer_print();
			if (tests[i].sleep) {
				surface = osd_get_visible_surface();
				osd_drawtext(surface, 100, 200, buf,
					     OSD_GREEN, OSD_BLACK, OSD_BLACK,
					     NULL);
				osd_drawtext(surface, 100, 80, tests[i].name,
					     OSD_GREEN, OSD_BLACK, OSD_BLACK,
					     NULL);
				sleep(tests[i].sleep);
			}
		} else {
			printf("failed at 0x%.8lx\n", lr);
			ret = -1;
		}
		osd_destroy_all_surfaces();
		i++;
	}

	return ret;
}
