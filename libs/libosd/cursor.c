/*
 *  Copyright (C) 2006, Jon Gettler
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
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <string.h>

#include "mvp_osd.h"

#include "osd.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

static int
cursor_move(osd_surface_t *surface, int x, int y)
{
	unsigned long rec[3];

	rec[0] = x;
	rec[1] = y;

	return ioctl(surface->fd, GFX_FB_OSD_CUR_MOVE_1, rec);
}

static int
cursor_display_surface(osd_surface_t *surface)
{
	unsigned long descriptor[2];
	int fd = surface->fd;

	descriptor[0] = surface->data.cursor.sfc.handle;
	descriptor[1] = 0; 
	
	if (ioctl(fd, GFX_FB_ATTACH, descriptor) < 0)
		return -1;

	return 0;
}

static int
cursor_add_color(osd_surface_t *surface, unsigned int c)
{
	cursor_palette_data_t palette;
	int ncolors = surface->data.cursor.ncolors;
	int i;
	unsigned char r, g, b, a;
	cursor_palette_t *colors = surface->data.cursor.colors;
	unsigned int attr[3];

	if (ncolors == CURSOR_MAX_COLORS)
		return -1;

	c2rgba(c, &r, &g, &b, &a);

	for (i=0; i<ncolors; i++) {
		if ((colors[i].alpha == a) && (colors[i].red == r) &&
		    (colors[i].green == g) && (colors[i].blue == b)) {
			return -1;
		}
	}

	colors[ncolors].alpha = a;
	colors[ncolors].red = r;
	colors[ncolors].green = g;
	colors[ncolors].blue = b;
	surface->data.cursor.ncolors = ncolors + 1;

	palette.handle = surface->data.cursor.sfc.handle;
	palette.start = 0;
	palette.count = surface->data.cursor.ncolors;
	palette.palette = &(surface->data.cursor.colors[0]);

	if (ioctl(surface->fd, GFX_FB_SET_PALETTE, &palette) < 0)
		return -1;

	for (i=0; i<surface->data.cursor.ncolors; i++) {
		attr[0] = surface->data.cursor.sfc.handle;
		attr[1] = i;
		attr[2] = 1;

		if (ioctl(surface->fd, GFX_FB_OSD_CUR_SETATTR, &attr) < 0)
			return -1;
	}

	return 0;
}

static int
find_color(osd_surface_t *surface, unsigned int c)
{
	cursor_palette_t *colors = surface->data.cursor.colors;
	unsigned char r, g, b, a;
	int i;

	c2rgba(c, &r, &g, &b, &a);

	for (i=0; i<surface->data.cursor.ncolors; i++) {
		if ((colors[i].alpha == a) && (colors[i].red == r) &&
		    (colors[i].green == g) && (colors[i].blue == b)) {
			return i;
		}
	}

	return -1;
}

static inline void
draw_pixel(osd_surface_t *surface, int x, int y, unsigned char pixel)
{
	int k;

	k = (y * surface->width) + x;

	if ((k%2) == 0) {
		k = k/2;
		pixel = (surface->data.cursor.base[k] & 0x0f) | (pixel << 4);
	} else {
		k = k/2;
		pixel = (surface->data.cursor.base[k] & 0xf0) | pixel;
	}

	surface->data.cursor.base[k] = pixel;
}

static int
cursor_draw_pixel(osd_surface_t *surface, int x, int y, unsigned int c)
{
	int pixel;

	if ((pixel=find_color(surface, c)) == -1) {
		return -1;
	}

	draw_pixel(surface, x, y, (unsigned char)pixel);

	return 0;
}

static int
cursor_fill_rect(osd_surface_t *surface, int x, int y, int width, int height, 
		 unsigned int c)
{
	int pixel;
	int X, Y;

	if ((pixel=find_color(surface, c)) == -1) {
		return -1;
	}

	for (X=0; X<width; X++) {
		for (Y=0; Y<height; Y++) {
			draw_pixel(surface, X+x, Y+y, (unsigned char)pixel);
		}
	}

	return 0;
}

static int
cursor_destroy_surface(osd_surface_t *surface)
{
	int fd = surface->fd;

	if (surface->data.cursor.base)
		munmap(surface->data.cursor.base,
		       surface->data.cursor.map.map[0].size);

	if (ioctl(fd, GFX_FB_SFC_FREE, surface->data.cursor.sfc.handle) < 0)
		return -1;

	return 0;
}

static osd_func_t fp = {
	.move = cursor_move,
	.display = cursor_display_surface,
	.destroy = cursor_destroy_surface,
	.draw_pixel = cursor_draw_pixel,
	.palette_add_color = cursor_add_color,
	.fill_rect = cursor_fill_rect,
};

void
cursor_init(void)
{
}

osd_surface_t*
cursor_create(int w, int h, unsigned long color)
{
	osd_surface_t *surface;
	int i;
	static int fd = -1;

	if (w == -1)
		w = full_width;
	if (h == -1)
		h = full_height;

	PRINTF("%s(): stbgfx %d\n", __FUNCTION__, stbgfx);

	if (stbgfx < 0) {
		if ((stbgfx=open("/dev/stbgfx", O_RDWR)) < 0)
			return NULL;
		gfx_init();
	}
	if (fd < 0) {
		if ((fd=open("/dev/stbgfx", O_RDWR)) < 0) {
			printf("dev open failed!\n");
			return NULL;
		}
	}

	PRINTF("%s(): stbgfx %d\n", __FUNCTION__, stbgfx);

	if ((surface=malloc(sizeof(*surface))) == NULL)
		return NULL;
	memset(surface, 0, sizeof(*surface));

	memset(&surface->data.cursor.sfc, 0, sizeof(surface->data.cursor.sfc));
	surface->data.cursor.sfc.width = w;
	surface->data.cursor.sfc.height = h;
	surface->data.cursor.sfc.background = color;

	surface->data.cursor.sfc.flags = 0x102008;
	surface->data.cursor.sfc.unknown = 0;

	if (ioctl(fd, GFX_FB_SFC_ALLOC, &surface->data.cursor.sfc) != 0) {
		goto err;
	}

	memset(&surface->data.cursor.map, 0, sizeof(surface->data.cursor.map));
	surface->data.cursor.map.map[0].unknown = surface->data.cursor.sfc.handle;
	if (ioctl(fd, GFX_FB_MAP, &surface->data.cursor.map) != 0)
		goto err;
	if ((surface->data.cursor.base=mmap(NULL,surface->data.cursor.map.map[0].size,
				   PROT_READ|PROT_WRITE, MAP_SHARED, fd,
				   surface->data.cursor.map.map[0].addr)) == MAP_FAILED)
		goto err;

	surface->fd = fd;

	surface->type = OSD_CURSOR;
	surface->fp = &fp;

	surface->width = w;
	surface->height = h;

	PRINTF("surface 0x%.8x created of size %d x %d   [%d]\n",
	       surface, w, h, surface->data.cursor.map.map[0].size);

	i = 0;
	while ((all[i] != NULL) && (i < OSD_MAX_SURFACES))
		i++;
	if (i < OSD_MAX_SURFACES)
		all[i] = surface;

	cursor_add_color(surface, color);
	osd_fill_rect(surface, 0, 0, w, h, color);

	return surface;

 err:
	if (surface)
		free(surface);

	return NULL;
}
