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
#include "fb.h"

static osd_surface_t *fb = NULL;

static int fd;

static int
fb_add_color(osd_surface_t *surface, unsigned int c)
{
	unsigned char r, g, b, a;
	int index;

	if (surface->data.fb.colors == FB_MAX_COLORS) {
		return -1;
	}

	index = surface->data.fb.colors++;

	c2rgba(c, &r, &g, &b, &a);

	surface->data.fb.red[index+32] = r << 8;
	surface->data.fb.blue[index+32] = b << 8;
	surface->data.fb.green[index+32] = g << 8;

	if (ioctl(surface->fd, FBIOPUTCMAP, &surface->data.fb.map) != 0) {
		return -1;
	}

	return 0;

}

static int
find_color(osd_surface_t *surface, unsigned int c)
{
	unsigned char r, g, b, a;
	int i;

	c2rgba(c, &r, &g, &b, &a);

	for (i=0; i<surface->data.fb.colors; i++) {
		if ((r == (surface->data.fb.red[i+32] >> 8)) &&
		    (b == (surface->data.fb.blue[i+32] >> 8)) &&
		    (g == (surface->data.fb.green[i+32] >> 8))) {
			return i+32;
		}
	}

	return -1;
}

static inline void
draw_pixel(osd_surface_t *surface, int x, int y, unsigned char pixel)
{
	int k;

	k = (y * surface->data.fb.finfo.line_length) + x;

	*((surface->data.fb.base) + k) = pixel;
}

int
fb_draw_pixel(osd_surface_t *surface, int x, int y, unsigned int c)
{
	int pixel;

	if ((pixel=find_color(surface, c)) == -1) {
		return -1;
	}

	draw_pixel(surface, x, y, (unsigned char)pixel);

	return 0;
}

int
fb_draw_image(osd_surface_t *surface, osd_indexed_image_t *image, int x, int y)
{
	int i, X, Y;

	memset(&surface->data.fb.map, 0, sizeof(surface->data.fb.map));

	for (i=0; i<image->colors; i++) {
		surface->data.fb.red[i+32] = image->red[i] << 8;
		surface->data.fb.blue[i+32] = image->blue[i] << 8;
		surface->data.fb.green[i+32] = image->green[i] << 8;
	}

	surface->data.fb.colors = image->colors;
	surface->data.fb.map.start = 0;
	surface->data.fb.map.len = 256;

	surface->data.fb.map.red = (__u16*)&surface->data.fb.red;
	surface->data.fb.map.blue = (__u16*)&surface->data.fb.blue;
	surface->data.fb.map.green = (__u16*)&surface->data.fb.green;
	surface->data.fb.map.transp = NULL;

	if (ioctl(surface->fd, FBIOPUTCMAP, &surface->data.fb.map) != 0) {
		return -1;
	}

	for (X=0; X<image->width; X++) {
		for (Y=0; Y<image->height; Y++) {
			draw_pixel(surface, x+X, y+Y,
				   image->image[(Y*image->width)+X]);
		}
	}

	return 0;
}

static int
fb_destroy_surface(osd_surface_t *surface)
{
	return 0;
}

static int
fb_display_surface(osd_surface_t *surface)
{
	visible = surface;

	return 0;
}

static int
fb_fill_rect(osd_surface_t *surface, int x, int y, int width, int height, 
	     unsigned int c)
{
	return 0;
}

static unsigned int
fb_read_pixel(osd_surface_t *surface, int x, int y)
{
	unsigned int c;
	int r, g, b, i;

	if ((x < 0) || (x >= surface->width))
		return -1;
	if ((y < 0) || (y >= surface->height))
		return -1;

	i = surface->data.fb.base[(y*surface->width) + x];
	r = surface->data.fb.red[i] >> 8;
	g = surface->data.fb.green[i] >> 8;
	b = surface->data.fb.blue[i] >> 8;
	c = rgba2c(r, g, b, 0xff);

	return c;
}

static osd_func_t fp = {
	.display = fb_display_surface,
	.destroy = fb_destroy_surface,
	.palette_add_color = fb_add_color,
	.draw_indexed_image = fb_draw_image,
	.draw_pixel = fb_draw_pixel,
	.read_pixel = fb_read_pixel,
	.fill_rect = fb_fill_rect,
};

osd_surface_t*
fb_create(int w, int h, unsigned long color)
{
	osd_surface_t *surface;
	int x, y;

	if (fb) {
		return NULL;
	}

	if ((surface=malloc(sizeof(*surface))) == NULL)
		return NULL;
	memset(surface, 0, sizeof(*surface));

	if ((surface->fd=open("/dev/fb0", O_RDWR)) < 0) {
		goto err;
	}

	surface->type = OSD_FB;

	if (ioctl(surface->fd, FBIOGET_FSCREENINFO, &surface->data.fb.finfo)) {
		goto err;
	}

	if (ioctl(surface->fd, FBIOGET_FSCREENINFO, &surface->data.fb.finfo)) {
		goto err;
	}

	if ((surface->data.fb.base=mmap(0, surface->data.fb.finfo.smem_len,
					PROT_READ | PROT_WRITE, MAP_SHARED,
					surface->fd, 0)) == MAP_FAILED) {
		goto err;
	}

	fd = surface->fd;
	surface->fp = &fp;

	surface->width = w;
	surface->height = h;

	fb_add_color(surface, color);

	for (x=0; x<w; x++) {
		for (y=0; y<h; y++) {
			fb_draw_pixel(surface, x, y, color);
		}
	}

	return surface;

 err:
	if (surface)
		free(surface);

	return NULL;
}

int
fb_init(void)
{
	return 0;
}
