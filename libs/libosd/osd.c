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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

#include "mvp_osd.h"

#include "osd.h"
#include "font.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

#if 1
/*
 * Currently, the following font is available.  Add more by using the
 * bdftobogl perl script that comes with bogl to convert X11 BDF fonts.
 *
 * XXX: This should change to support microwindows loadable fonts!!!!!
 */
extern osd_font_t font_CaslonRoman_1_25;
osd_font_t *osd_default_font = &font_CaslonRoman_1_25;
#endif

int full_width = 720, full_height = 480;

osd_surface_t *all[OSD_MAX_SURFACES];

osd_surface_t *visible = NULL;

int
osd_draw_pixel(osd_surface_t *surface, int x, int y, unsigned int c)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->draw_pixel)
		return surface->fp->draw_pixel(surface, x, y, c);
	else
		return -1;
}

int
osd_draw_pixel_ayuv(osd_surface_t *surface, int x, int y, unsigned char a,
		    unsigned char Y, unsigned char U, unsigned char V)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->draw_pixel_ayuv) {
		return surface->fp->draw_pixel_ayuv(surface, x, y, a, Y, U, V);
	} else {
		unsigned char r, g, b;
		unsigned int c;
		yuv2rgb(Y, U, V, &r, &g, &b);
		c = rgba2c(r, g, b, a);
		return osd_draw_pixel(surface, x, y, c);
	}
}

int
osd_draw_pixel_list(osd_surface_t *surface, int *x, int *y, int n,
		    unsigned int c)
{
	int i;

	if (surface == NULL)
		return -1;

	for (i=0; i<n; i++) {
		if (osd_draw_pixel(surface, x[i], y[i], c) < 0)
			return -1;
	}

	return 0;
}

int
osd_draw_line(osd_surface_t *surface, int x1, int y1, int x2, int y2,
	      unsigned int c)
{
	int dx, dy;
	double t = 0.5;

	if (surface == NULL)
		return -1;

	dy = y2 - y1;
	dx = x2 - x1;

	osd_draw_pixel(surface, x1, y1, c);

	if (abs(dx) > abs(dy)) {
		double m = (double)dy / (double)dx;
		t += y1;
		dx = (dx < 0) ? -1 : 1;
		m *= dx;
		while (x1 != x2) {
			x1 += dx;
			t += m;
			osd_draw_pixel(surface, x1, (int)t, c);
		}
	} else {
		double m = (double)dx / (double)dy;
		t += x1;
		dy = (dy < 0) ? -1 : 1;
		m *= dy;
		while (y1 != y2) {
			y1 += dy;
			t += m;
			osd_draw_pixel(surface, (int)t, y1, c);
		}
	}

	return 0;
}

int
osd_draw_polygon(osd_surface_t *surface, int *x, int *y, int n,
		 unsigned long c)
{
	int i;

	if (surface == NULL)
		return -1;

	if (n < 3)
		return -1;

	for (i=0; i<n; i++) {
		int sx, sy, dx, dy;

		sx = x[i];
		sy = y[i];
		if ((i+1) == n) {
			dx = x[0];
			dy = y[0];
		} else {
			dx = x[i+1];
			dy = y[i+1];
		}
		if (osd_draw_line(surface, sx, sy, dx, dy, c) < 0)
			return -1;
	}

	return 0;
}

int
osd_draw_circle(osd_surface_t *surface, int xc, int yc, int radius,
		int filled, unsigned long c)
{
	int x, y, d;

	if (surface == NULL)
		return -1;

	x = 0;
	y = radius;
	d = 3 - (2*radius);

	while (x <= y) {
		if (filled == 0) {
			osd_draw_pixel(surface, xc+x, yc+y, c);
			osd_draw_pixel(surface, xc-x, yc+y, c);
			osd_draw_pixel(surface, xc+x, yc-y, c);
			osd_draw_pixel(surface, xc-x, yc-y, c);
			osd_draw_pixel(surface, xc+y, yc+x, c);
			osd_draw_pixel(surface, xc-y, yc+x, c);
			osd_draw_pixel(surface, xc+y, yc-x, c);
			osd_draw_pixel(surface, xc-y, yc-x, c);
		} else {
			osd_fill_rect(surface, xc-x, yc-y, 2*x, y, c);
			osd_fill_rect(surface, xc-y, yc-x, y, 2*x, c);
			osd_fill_rect(surface, xc-x, yc-y, x, 2*y, c);
			osd_fill_rect(surface, xc-y, yc-x, 2*y, x, c);
			osd_fill_rect(surface, xc, yc, y, x, c);
			osd_fill_rect(surface, xc, yc, x, y, c);
		}

		if (d < 0) {
			d = d + (4*x) + 6;
		} else {
			d = d + (4*(x-y)) + 10;
			y-=1;
		}
		x++;
	}

	return 0;
}

unsigned int
osd_read_pixel(osd_surface_t *surface, int x, int y)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->read_pixel)
		return surface->fp->read_pixel(surface, x, y);
	else
		return -1;
}

int
osd_draw_horz_line(osd_surface_t *surface, int x1, int x2, int y,
		   unsigned int c)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->draw_horz_line)
		return surface->fp->draw_horz_line(surface, x1, x2, y, c);
	else
		return osd_draw_line(surface, x1, y, x2, y, c);
}

int
osd_draw_vert_line(osd_surface_t *surface, int x, int y1, int y2,
		   unsigned int c)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->draw_vert_line)
		return surface->fp->draw_vert_line(surface, x, y1, y2, c);
	else
		return osd_draw_line(surface, x, y1, x, y2, c);
}

int
osd_fill_rect(osd_surface_t *surface, int x, int y, int w, int h,
	      unsigned int c)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->fill_rect)
		return surface->fp->fill_rect(surface, x, y, w, h, c);
	else
		return -1;
}

int
osd_draw_rect(osd_surface_t *surface, int x, int y, int w, int h,
	      unsigned int c, int fill)
{
	if (surface == NULL)
		return -1;

	if (fill) {
		return osd_fill_rect(surface, x, y, w, h, c);
	}

	osd_draw_line(surface, x, y, x+w, y, c);
	osd_draw_line(surface, x, y, x, y+h, c);
	osd_draw_line(surface, x+w, y, x+w, y+h, c);
	osd_draw_line(surface, x, y+h, x+w, y+h, c);

	return 0;
}

static int
blit_copy(osd_surface_t *dstsfc, int dstx, int dsty,
	  osd_surface_t *srcsfc, int srcx, int srcy, int w, int h)
{
	int x, y;
	unsigned int c;

	if ((srcsfc->fp->read_pixel == NULL) ||
	    (dstsfc->fp->draw_pixel == NULL))
		return -1;

	if ((dstx <= 0) || ((dstx+w) >= dstsfc->width))
		return -1;
	if ((dsty <= 0) || ((dsty+h) >= dstsfc->height))
		return -1;
	if ((srcx <= 0) || ((srcx+w) >= srcsfc->width))
		return -1;
	if ((srcy <= 0) || ((srcy+h) >= srcsfc->height))
		return -1;

	for (x=0; x<w; x++) {
		for (y=0; y<h; y++) {
			c = srcsfc->fp->read_pixel(srcsfc, srcx+x, srcy+y);
			if (dstsfc->fp->palette_add_color)
				dstsfc->fp->palette_add_color(dstsfc, c);
			if (dstsfc->fp->draw_pixel(dstsfc,
						   dstx+x, dsty+y, c) != 0) {
				return -1;
			}
		}
	}

	return 0;
}

int
osd_blit(osd_surface_t *dstsfc, int dstx, int dsty,
	 osd_surface_t *srcsfc, int srcx, int srcy, int w, int h)
{
	if ((dstsfc == NULL) || (srcsfc == NULL))
		return -1;

	if (dstsfc->type != srcsfc->type) {
		return blit_copy(dstsfc, dstx, dsty,
				 srcsfc, srcx, srcy, w, h);
	}

	if (dstsfc == srcsfc)
		return -1;

	if (dstsfc->fp->blit)
		return dstsfc->fp->blit(dstsfc, dstx, dsty,
					srcsfc, srcx, srcy, w, h);
	else
		return -1;
}

int
osd_drawtext(osd_surface_t *surface, int x, int y, const char *str,
	     unsigned int fg, unsigned int bg, int background, void *FONT)
{
	osd_font_t *font = FONT;
	int h, n, i;
	int Y, X, cx, swidth;

	if (surface == NULL)
		return -1;

#if 1
	if (font == NULL)
		font = osd_default_font;
#endif

	n = strlen(str);
	h = font->height;

	swidth = 0;
	for (i=0; i<n; i++) {
		swidth += font->width[(int)str[i]];
	}

	if (background) {
		int w = swidth / n;
		osd_draw_rect(surface, x-w, y-h/2, (2*w)+swidth, h*2, bg, 1);
	}

	X = 0;
	cx = 0;
	for (i=0; i<n; i++) {
		unsigned char c = str[i];
		unsigned long *character = &font->content[font->offset[c]];
		int w = font->width[c];
		int pixels = 0;

		PRINTF("draw '%c' width %d at x %d\n", c, w, x+cx);

		for (X=0; X<w; X++) {
			for (Y=0; Y<h; Y++) {
				if ((character[Y] >> (32 - X)) & 0x1) {
					osd_draw_pixel(surface, x+X+cx, y+Y,
						       fg);
					pixels++;
				}
			}
		}

		PRINTF("\tletter contained %d pixels\n", pixels);

		cx += w;
	}

	PRINTF("drawing lines at %d and %d\n", y, y+h);

	return 0;
}

osd_surface_t*
osd_create_surface(int w, int h, unsigned long color, osd_type_t type)
{
	switch (type) {
	case OSD_FB:
		return fb_create(w, h, color);
		break;
	case OSD_CURSOR:
		return cursor_create(w, h, color);
		break;
	case OSD_GFX:
		return gfx_create(w, h, color);
		break;
	}

	return NULL;
}

int
osd_set_screen_size(int w, int h)
{
	if ((w > 720) || (h > 576))
		return -1;

	full_width = w;
	full_height = h;

	return 0;
}

int
osd_get_surface_size(osd_surface_t *surface, int *w, int *h)
{
	if (surface == NULL)
		return -1;

	if (w)
		*w = surface->width;
	if (h)
		*h = surface->height;

	return 0;
}

int
osd_display_surface(osd_surface_t *surface)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->display)
		return surface->fp->display(surface);
	else
		return -1;
}

int
osd_draw_indexed_image(osd_surface_t *surface,
		       osd_indexed_image_t *image, int x, int y)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->draw_indexed_image)
		return surface->fp->draw_indexed_image(surface, image, x, y);
	else
		return -1;
}

int
osd_destroy_surface(osd_surface_t *surface)
{
	int i;

	if (surface == NULL)
		return -1;

	if (surface == visible)
		visible = NULL;

	i = 0;
	while ((all[i] != surface) && (i < OSD_MAX_SURFACES))
		i++;
	if (i < OSD_MAX_SURFACES)
		all[i] = NULL;

	if (surface->fp->destroy)
		surface->fp->destroy(surface);

	free(surface);

	return 0;
}

void
osd_destroy_all_surfaces(void)
{
	int i;

	for (i=0; i<OSD_MAX_SURFACES; i++) {
		if (all[i])
			osd_destroy_surface(all[i]);
	}

	visible = NULL;
}

osd_surface_t*
osd_get_visible_surface(void)
{
	return visible;
}

int
osd_palette_add_color(osd_surface_t *surface, unsigned int c)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->palette_add_color)
		return surface->fp->palette_add_color(surface, c);
	else
		return -1;
}

int
osd_blend(osd_surface_t *surface, int x, int y, int w, int h,
	  osd_surface_t *surface2, int x2, int y2, int w2, int h2,
	  unsigned long colour)
{
	if (surface == NULL)
		return -1;

	if (surface->type != surface2->type)
		return -1;

	if (surface->fp->blend)
		return surface->fp->blend(surface, x, y, w, h,
					  surface2, x2, y2, w2, h2, colour);
	else
		return -1;
}

int
osd_afillblt(osd_surface_t *surface,
	     int x, int y, int w, int h, unsigned long colour)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->afillblt)
		return surface->fp->afillblt(surface, x, y, w, h, colour);
	else
		return -1;
}

int
osd_clip(osd_surface_t *surface, int left, int top, int right, int bottom)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->clip)
		return surface->fp->clip(surface, left, top, right, bottom);
	else
		return -1;
}

int
osd_get_visual_device_control(osd_surface_t *surface)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->get_dev_control)
		return surface->fp->get_dev_control(surface);
	else
		return -1;
}

int
osd_cur_set_attr(osd_surface_t *surface, int x, int y)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->set_attr)
		return surface->fp->set_attr(surface, x, y);
	else
		return -1;
}

int
osd_move(osd_surface_t *surface, int x, int y)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->move)
		return surface->fp->move(surface, x, y);
	else
		return -1;
}

int
osd_get_engine_mode(osd_surface_t *surface)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->get_engine_mode)
		return surface->fp->get_engine_mode(surface);
	else
		return -1;
}

int
osd_set_engine_mode(osd_surface_t *surface, int mode)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->set_engine_mode)
		return surface->fp->set_engine_mode(surface, mode);
	else
		return -1;
}

int
osd_reset_engine(osd_surface_t *surface)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->reset_engine)
		return surface->fp->reset_engine(surface);
	else
		return -1;
}

int
osd_set_display_control(osd_surface_t *surface, int type, int value)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->set_display_control)
		return surface->fp->set_display_control(surface, type, value);
	else
		return -1;
}

int
osd_get_display_control(osd_surface_t *surface, int type)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->get_display_control)
		return surface->fp->get_display_control(surface, type);
	else
		return -1;
}

int
osd_get_display_options(osd_surface_t *surface)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->get_display_options)
		return surface->fp->get_display_options(surface);
	else
		return -1;
}

int
osd_set_display_options(osd_surface_t *surface, unsigned char option)
{
	if (surface == NULL)
		return -1;

	if (surface->fp->set_display_options)
		return surface->fp->set_display_options(surface, option);
	else
		return -1;
}
