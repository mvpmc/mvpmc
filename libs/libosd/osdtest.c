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

#define OSD_COLOR(r,g,b,a)	((a<<24) | (r<<16) | (g<<8) | b)

#define OSD_WHITE	OSD_COLOR(255,255,255,255)
#define OSD_RED		OSD_COLOR(255,0,0,255)
#define OSD_BLUE	OSD_COLOR(0,0,255,255)
#define OSD_GREEN	OSD_COLOR(0,255,0,255)
#define OSD_BLACK	OSD_COLOR(0,0,0,255)

static int width = 720, height = 480;

static struct timeval start, end, delta;

#define timer_start()	fflush(stdout); gettimeofday(&start, NULL)

#define timer_end()	gettimeofday(&end, NULL)

#define timer_print()	timersub(&end,&start,&delta); \
			snprintf(buf, sizeof(buf), "%5.2f seconds\n", \
			         delta.tv_sec + (delta.tv_usec/1000000.0)); \
			printf(buf)

typedef struct {
	char *name;
	int sleep;
	int (*func)(char*);
} tester_t;

static int
test_sanity(char *name)
{
	osd_surface_t *surface = NULL;
	unsigned long c1, c2, c3;

	printf("testing osd sanity\t");

	timer_start();

	if ((surface=osd_create_surface(width, height, 0)) == NULL)
		return -1;

	c1 = rand() | 0xff000000;
	c2 = rand() | 0xff000000;
	c3 = rand() | 0xff000000;

	if (osd_drawtext(surface, 0, 0, "Hello World!", c1, c2, c3, NULL) < 0)
		goto err;

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
		if ((surface=osd_create_surface(width, height, 0)) == NULL)
			goto err;
		if (i == 0) {
			if (osd_display_surface(surface) < 0)
				goto err;
			if (osd_drawtext(surface, 100, 200,
					 "Creating surfaces!",
					 OSD_GREEN, OSD_BLACK, OSD_BLACK,
					 NULL) < 0)
				goto err;
		} else {
			if (osd_destroy_surface(surface) < 0)
				goto err;
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

	if ((surface=osd_create_surface(width, height, 0)) == NULL)
		goto err;

	for (i=0; i<height; i+=50) {
		unsigned long c1, c2, c3;

		c1 = rand() | 0xff000000;
		c2 = rand() | 0xff000000;
		c3 = rand() | 0xff000000;

		if (osd_drawtext(surface, i, i, "Hello World!",
				 c1, c2, c3, NULL) < 0)
			goto err;
	}

	if (osd_display_surface(surface) < 0)
		goto err;

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

	if ((surface=osd_create_surface(width, height, 0)) == NULL)
		goto err;

	if (osd_display_surface(surface) < 0)
		goto err;

	for (i=0; i<n; i++) {
		int x, y, w, h;
		unsigned long c;

		x = rand() % width;
		y = rand() % height;
		w = rand() % (width - x);
		h = rand() % (height - y);
		c = rand() | 0xff000000;

		osd_fill_rect(surface, x, y, w, h, c);
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

	if ((surface=osd_create_surface(width, height, 0)) == NULL)
		goto err;

	if (osd_display_surface(surface) < 0)
		goto err;

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
				goto err;
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

	if ((surface=osd_create_surface(width, height, OSD_BLUE)) == NULL)
		goto err;

	if (osd_set_engine_mode(surface, 0) < 0)
		goto err;

	if (osd_display_surface(surface) < 0)
		goto err;

	if (osd_get_display_control(surface, 2) < 0)
		goto err;
	if (osd_get_display_control(surface, 3) < 0)
		goto err;
	if (osd_get_display_control(surface, 4) < 0)
		goto err;
	if (osd_get_display_control(surface, 5) < 0)
		goto err;
	if (osd_get_display_control(surface, 7) < 0)
		goto err;
	if (osd_get_display_options(surface) < 0)
		goto err;

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

	if ((surface=osd_create_surface(width, height, 0)) == NULL)
		goto err;
	if ((hidden=osd_create_surface(width, height, 0)) == NULL)
		goto err;

	if (osd_display_surface(surface) < 0)
		goto err;

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
					goto err;
			}
			if (osd_blit(surface, x1, y1, hidden, x1, y1, w, h) < 0)
				goto err;
		}
	}

	timer_end();

	return 0;

 err:
	return -1;
}

static tester_t tests[] = {
	{ "sanity",		0,	test_sanity },
	{ "create surfaces",	0,	test_create_surfaces },
	{ "text",		2,	test_text },
	{ "rectangles",		2,	test_rectangles },
	{ "lines",		2,	test_lines },
	{ "display control",	2,	test_display_control },
	{ "blit",		2,	test_blit },
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
			printf("failed\n");
		}
		osd_destroy_all_surfaces();
		i++;
	}

	return 0;
}
