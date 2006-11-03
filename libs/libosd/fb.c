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

#include "surface.h"

inline void
fb_draw_pixel(osd_surface_t *surface, int x, int y, unsigned char pixel)
{
	int k;

	k = (y * surface->finfo.line_length) + x;

	*((surface->base[0]) + k) = pixel;
}

int
fb_draw_image(osd_surface_t *surface, osd_fb_image_t *image, int x, int y)
{
	int i, X, Y;
	struct fb_cmap map;
	static __u16 red[256];
	static __u16 blue[256];
	static __u16 green[256];

	memset(&map, 0, sizeof(map));

	for (i=0; i<image->colors; i++) {
		red[i+32] = image->red[i] << 8;
		blue[i+32] = image->blue[i] << 8;
		green[i+32] = image->green[i] << 8;
	}

	map.start = 0;
	map.len = 256;

	map.red = (__u16*)&red;
	map.blue = (__u16*)&blue;
	map.green = (__u16*)&green;
	map.transp = NULL;

	if (ioctl(surface->fd, FBIOPUTCMAP, &map) != 0) {
		return -1;
	}

	for (X=0; X<image->width; X++) {
		for (Y=0; Y<image->height; Y++) {
			fb_draw_pixel(surface, x+X, y+Y,
				      image->image[(Y*image->width)+X]);
		}
	}

	return 0;
}

osd_surface_t*
fb_create(int w, int h, unsigned long color)
{
	osd_surface_t *surface;
	int x, y;

	if ((surface=malloc(sizeof(*surface))) == NULL)
		return NULL;
	memset(surface, 0, sizeof(*surface));

	if ((surface->fd=open("/dev/fb0", O_RDWR)) < 0) {
		goto err;
	}

	surface->type = OSD_FB;

	if (ioctl(surface->fd, FBIOGET_FSCREENINFO, &surface->finfo)) {
		goto err;
	}

	if (ioctl(surface->fd, FBIOGET_FSCREENINFO, &surface->finfo)) {
		goto err;
	}

	if ((surface->base[0]=mmap(0, surface->finfo.smem_len,
				   PROT_READ | PROT_WRITE,
				   MAP_SHARED,
				   surface->fd, 0)) == MAP_FAILED) {
		goto err;
	}

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
