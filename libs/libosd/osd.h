/*
 *  Copyright (C) 2004-2006, BtB, Jon Gettler
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

#ifndef OSD_H
#define OSD_H

/** \file osd.h
 * On-Screen-Display hardware interface.
 */

typedef struct osd_func_s osd_func_t;

#include "fb.h"
#include "gfx.h"
#include "cursor.h"

#define full_width	__osd_full_width
#define full_height	__osd_full_height
#define all		__osd_all
#define visible		__osd_visible

struct osd_func_s {
	int (*destroy)(osd_surface_t*);
	int (*display)(osd_surface_t*);
	int (*undisplay)(osd_surface_t*);
	int (*palette_init)(osd_surface_t*);
	int (*palette_add_color)(osd_surface_t*, unsigned int);
	int (*draw_pixel)(osd_surface_t*, int, int, unsigned int);
	int (*draw_pixel_ayuv)(osd_surface_t*, int, int,
			       unsigned char, unsigned char,
			       unsigned char, unsigned char);
	unsigned int (*read_pixel)(osd_surface_t*, int, int);
	int (*draw_horz_line)(osd_surface_t*, int, int, int, unsigned int);
	int (*draw_vert_line)(osd_surface_t*, int, int, int, unsigned int);
	int (*fill_rect)(osd_surface_t*, int, int, int, int, unsigned int);
	int (*blit)(osd_surface_t*, int, int, osd_surface_t*, int, int,
		    int, int);
	int (*draw_indexed_image)(osd_surface_t*, osd_indexed_image_t*,
				  int, int);
	int (*blend)(osd_surface_t*, int, int, int, int,
		     osd_surface_t*, int, int, int, int, unsigned long);
	int (*afillblt)(osd_surface_t*, int, int, int, int, unsigned long);
	int (*clip)(osd_surface_t*, int, int, int, int);
	int (*get_dev_control)(osd_surface_t*);
	int (*set_attr)(osd_surface_t*, int, int);
	int (*move)(osd_surface_t*, int, int);
	int (*get_engine_mode)(osd_surface_t*);
	int (*set_engine_mode)(osd_surface_t*, int);
	int (*reset_engine)(osd_surface_t*);
	int (*set_display_control)(osd_surface_t*, int, int);
	int (*get_display_control)(osd_surface_t*, int);
	int (*get_display_options)(osd_surface_t*);
	int (*set_display_options)(osd_surface_t*, unsigned char);
};

/**
 * OSD Surface.
 */
struct osd_surface_s {
	int fd;
	osd_type_t type;
	osd_func_t *fp;
	int width;
	int height;
	union {
		fb_data_t fb;
		gfx_data_t gfx;
		cursor_data_t cursor;
	} data;
};

/**
 * Convert a color from RGB to YUV.
 * \param r red
 * \param g green
 * \param b blue
 * \param[out] y y
 * \param[out] u u
 * \param[out] v v
 */
extern void rgb2yuv(unsigned char r, unsigned char g, unsigned char b,
		    unsigned char *y, unsigned char *u, unsigned char *v);

/**
 * Convert a color from YUV to RGV.
 * \param y y
 * \param u u
 * \param v v
 * \param[out] r red
 * \param[out] g green
 * \param[out] b blue
 */
extern void yuv2rgb(unsigned char y, unsigned char u, unsigned char v,
		    unsigned char *r, unsigned char *g, unsigned char *b);

static inline unsigned long
rgba2c(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	return (a<<24) | (r<<16) | (g<<8) | b;
}

static inline void
c2rgba(unsigned long c, unsigned char *r, unsigned char *g, unsigned char *b,
       unsigned char *a)
{
	*a = (c & 0xff000000) >> 24;
	*r = (c & 0x00ff0000) >> 16;
	*g = (c & 0x0000ff00) >> 8;
	*b = (c & 0x000000ff);
}

extern int full_width, full_height;

extern osd_surface_t *all[];
extern osd_surface_t *visible;

#define OSD_MAX_SURFACES	128

#endif /* STBGFX_H */
