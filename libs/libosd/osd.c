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

#include "surface.h"
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

/*
 */
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

static int
osd_fillblt(osd_surface_t *surface, int x, int y, int width, int height, 
	    unsigned int c)
{
	osd_fillblt_t fblt;

	fblt.handle = surface->sfc.handle;
	fblt.x = x;
	fblt.y = y;
	fblt.width = width;
	fblt.height = height;
	fblt.colour = c;

	return ioctl(surface->fd, GFX_FB_OSD_FILLBLT, &fblt);
}

static int
osd_bitblt(osd_surface_t *dstsfc, int xd, int yd,
	   osd_surface_t *srcsfc, int x, int y, int width, int height)
{
	osd_bitblt_t fblt;

	memset(&fblt, 0, sizeof(fblt));

	fblt.dst_handle = dstsfc->sfc.handle;
	fblt.dst_x = xd;
	fblt.dst_y = yd;

	fblt.src_handle = srcsfc->sfc.handle;
	fblt.src_x = x;
	fblt.src_y = y;

	fblt.width = width;
	fblt.height = height;

	fblt.u1 = 1;
	fblt.u2 = 0;
	fblt.u3 = 0x0f;

	return ioctl(dstsfc->fd, GFX_FB_OSD_BITBLT, &fblt);
}

int
osd_draw_pixel_ayuv(osd_surface_t *surface, int x, int y, unsigned char a,
		    unsigned char Y, unsigned char U, unsigned char V)
{
	int offset;
	unsigned int line, remainder;

	if (surface == NULL)
		return -1;

	if ((x >= surface->sfc.width) || (y >= surface->sfc.height))
		return -1;

	remainder = (surface->sfc.width % 4);
	if (remainder == 0)
		line = surface->sfc.width;
	else
		line = surface->sfc.width + (4 - remainder);

	offset = (y * line) + x;

	*(surface->base[0] + offset) = Y;
	*(surface->base[1] + (offset & 0xfffffffe)) = U;
	*(surface->base[1] + (offset & 0xfffffffe) + 1) = V;
	*(surface->base[2] + offset) = a;

	return 0;
}

int
osd_draw_pixel(osd_surface_t *surface, int x, int y, unsigned int c)
{
	unsigned char r, g, b, a, Y, U, V;

	if (surface == NULL)
		return -1;

	c2rgba(c, &r, &g, &b, &a);
	rgb2yuv(r, g, b, &Y, &U, &V);

	return osd_draw_pixel_ayuv(surface, x, y, a, Y, U, V);
}

int
osd_draw_line(osd_surface_t *surface, int x1, int y1, int x2, int y2,
	      unsigned int c)
{
	int x, y;
	int i = 0; 
	double dx, dy;

	if (surface == NULL)
		return -1;

	x = x1;
	y = y1;

	if (y2 == y1)
		dx = 1;
	else
		dx = (double)(x2 - x1) / (double)(y2 - y1);
	if (x2 == x1)
		dy = 1;
	else
		dy = (double)(y2 - y1) / (double)(x2 - x1);

	if ((x1 > x2) && (dx > 0)) {
		dx = dx * -1;
	}
	if ((x1 < x2) && (dx < 0)) {
		dx = dx * -1;
	}
	if ((y1 > y2) && (dy > 0)) {
		dy = dy * -1;
	}
	if ((y1 < y2) && (dy < 0)) {
		dy = dy * -1;
	}

	while (1) {
		x = x1 + dx * i;
		y = y1 + dy * i;
		if ((dx > 0) && (x > x2))
			break;
		if ((dx < 0) && (x < x2))
			break;
		if ((dy > 0) && (y > y2))
			break;
		if ((dy < 0) && (y < y2))
			break;
		if (osd_draw_pixel(surface, x, y, c) < 0) {
			return -1;
		}
		i++;
	}

	return 0;
}

unsigned int
osd_read_pixel(osd_surface_t *surface, int x, int y)
{
	int offset;
	unsigned char r, g, b, a, Y, U, V;
	unsigned int line, remainder;

	if (surface == NULL)
		return -1;

	if ((x >= surface->sfc.width) || (y >= surface->sfc.height))
		return 0;

	remainder = (surface->sfc.width % 4);
	if (remainder == 0)
		line = surface->sfc.width;
	else
		line = surface->sfc.width + (4 - remainder);

	offset = (y * line) + x;

	Y = *(surface->base[0] + offset);
	U = *(surface->base[1] + (offset & 0xfffffffe));
	V = *(surface->base[1] + (offset & 0xfffffffe) + 1);
	a = *(surface->base[2] + offset);

	yuv2rgb(Y, U, V, &r, &g, &b);

	PRINTF("YUVa: %d %d %d %d  RGB: %d %d %d\n", Y, U, V, a, r, g, b);

	return (a << 24) | (r << 16) | (g << 8) | b;
}

int
osd_draw_horz_line(osd_surface_t *surface, int x1, int x2, int y,
		   unsigned int c)
{
	if (surface == NULL)
		return -1;

	return osd_fillblt(surface, x1, y, x2-x1+1, 1, c);
}

int
osd_draw_vert_line(osd_surface_t *surface, int x, int y1, int y2,
		   unsigned int c)
{
	if (surface == NULL)
		return -1;

	return osd_fillblt(surface, x, y1, 1, y2-y1+1, c);
}

int
osd_fill_rect(osd_surface_t *surface, int x, int y, int w, int h,
	      unsigned int c)
{
	if (surface == NULL)
		return -1;

	return osd_fillblt(surface, x, y, w, h, c);
}

int
osd_blit(osd_surface_t *dstsfc, int dstx, int dsty,
	 osd_surface_t *srcsfc, int srcx, int srcy, int w, int h)
{
	if ((dstsfc == NULL) || (srcsfc == NULL))
		return -1;

	return osd_bitblt(dstsfc, dstx, dsty, srcsfc, srcx, srcy, w, h);
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
		osd_fillblt(surface, x-w, y-h/2, (2*w)+swidth, h*2, bg);
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

/*
 * XXX: this has not been tested!
 */
int
osd_blend(osd_surface_t *surface, int x, int y, int w, int h,
	  osd_surface_t *surface2, int x2, int y2, int w2, int h2,
	  unsigned long colour)
{
	osd_blend_t fblt;

	memset(&fblt, 0, sizeof(fblt));

	fblt.handle1 = surface->sfc.handle;
	fblt.x = x;
	fblt.y = y;
	fblt.w = w;
	fblt.h = h;

	fblt.handle2 = surface2->sfc.handle;
	fblt.x1 = x2;
	fblt.y1 = y2;
	fblt.w1 = w;
	fblt.h1 = h;

	fblt.colour1 = colour;

	return ioctl(surface->fd, GFX_FB_OSD_BLEND, &fblt);
}

/*
 * XXX: this has not been tested!
 */
int
osd_afillblt(osd_surface_t *surface,
	     int x, int y, int w, int h, unsigned long colour)
{
	osd_afillblt_t fblt;

	fblt.handle = surface->sfc.handle;
	fblt.x = x;
	fblt.y = y;
	fblt.w = w;
	fblt.h = h;

	fblt.colour1 = colour;
	fblt.colour2 = 0xffffffff;
	fblt.colour3 = 0xffffffff;
	fblt.colour4 = 0xffffffff;
	fblt.colour5 = 0xffffffff;
	fblt.colour6 = 0xffffffff;
	fblt.colour7 = 0xffffffff;

	fblt.c[0] = 255;
	fblt.c[1] = 255;

	return ioctl(surface->fd, GFX_FB_OSD_ADVFILLBLT, fblt);
}

/*
 * XXX: this has not been tested!
 */
int
osd_sfc_clip(osd_surface_t *surface,
	     int left, int top, int right, int bottom)
{
	osd_clip_rec_t rec;

	rec.handle = surface->sfc.handle;
	rec.left = left;
	rec.top = top;
	rec.bottom = bottom;
	rec.right = right;

	return ioctl(surface->fd, GFX_FB_OSD_SFC_CLIP, &rec);
}

/*
 * XXX: this has not been tested!
 */
int
osd_get_visual_device_control(osd_surface_t *surface)
{
	unsigned long parm[3];
	int ret;

	if ((ret=ioctl(surface->fd, GFX_FB_GET_VIS_DEV_CTL, &parm)) == 0) {
		printf("Get Visual Device control\n");
		printf("ret = %d, parm[0]=%lx,parm[1]=%ld,parm[2]=%ld\n",
		       ret, parm[0], parm[1], parm[2]);
	}

	return ret;
}

/*
 * XXX: this has not been tested!
 */
int
osd_cur_set_attr(osd_surface_t *surface, int x, int y)
{
	unsigned long int data[3];

	data[0] = surface->sfc.handle;
	data[1] = x;
	data[2] = y;

	return ioctl(surface->fd, GFX_FB_OSD_CUR_SETATTR, data);
}

/*
 * XXX: this has not been tested!
 */
int
move_cursor(osd_surface_t *surface, int x, int y)
{
	unsigned long rec[3];

	rec[0] = x;
	rec[1] = y;

	return ioctl(surface->fd, GFX_FB_OSD_CUR_MOVE_1, rec);
}

/*
 * XXX: this has not been tested!
 */
int
osd_get_engine_mode(osd_surface_t *surface)
{
	return ioctl(surface->fd, GFX_FB_GET_ENGINE_MODE);
}

/*
 * XXX: this has not been tested!
 */
int
osd_set_engine_mode(osd_surface_t *surface, int mode)
{
	return ioctl(surface->fd, GFX_FB_SET_ENGINE_MODE, mode);
}

/*
 * XXX: this has not been tested!
 */
int
osd_reset_engine(osd_surface_t *surface)
{
	return ioctl(surface->fd, GFX_FB_RESET_ENGINE);
}

/*
 * XXX: this has not been tested!
 */
int
osd_set_display_control(osd_surface_t *surface, int type, int value)
{
	osd_display_control_t ctrl;

	ctrl.type = type;
	ctrl.value = value;

	return ioctl(surface->fd, GFX_FB_SET_DISP_CTRL, &ctrl);
}

/*
 * XXX: this has not been tested!
 */
int
osd_get_display_control(osd_surface_t *surface, int type)
{
	osd_display_control_t ctrl;

	ctrl.type = type;
	ctrl.value = 0;

	if (ioctl(surface->fd, GFX_FB_GET_DISP_CTRL, &ctrl) < 0)
		return -1;

	return ctrl.value;
}

int
osd_get_display_options(osd_surface_t *surface)
{
	if (ioctl(surface->fd, GFX_FB_SET_DISPLAY, &surface->display) < 0)
		return -1;

	return surface->display.option;
}

int
osd_set_display_options(osd_surface_t *surface, unsigned char option)
{
	surface->display.option = option;

	return ioctl(surface->fd, GFX_FB_SET_DISPLAY, &surface->display);
}
