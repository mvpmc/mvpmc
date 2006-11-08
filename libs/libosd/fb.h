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

#ifndef OSD_FB_H
#define OSD_FB_H

#include <linux/fb.h>

#define FB_MAX_COLORS	223

typedef struct {
	struct fb_fix_screeninfo finfo;
	struct fb_cmap map;
	__u16 red[256];
	__u16 green[256];
	__u16 blue[256];
	int colors;
	unsigned char *base;
} fb_data_t;

#define fb_create	__osd_fb_create
#define fb_draw_pixel	__osd_fb_draw_pixel

extern osd_surface_t* fb_create(int w, int h, unsigned long color);
extern int fb_draw_pixel(osd_surface_t *surface, int x, int y, unsigned int c);

#endif /* OSD_FB_H */
