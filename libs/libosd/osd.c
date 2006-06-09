/*
 *  Copyright (C) 2004, BtB, Jon Gettler
 *  http://mvpmc.sourceforge.net/
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

#ident "$Id$"

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
osd_fillblt(osd_surface_t *sfc, int x, int y, int width, int height, 
	    unsigned int c)
{
	osd_fillblt_t fblt;

	fblt.handle = sfc->sfc.handle;
	fblt.x = x;
	fblt.y = y;
	fblt.width = width;
	fblt.height = height;
	fblt.colour = c;

	return ioctl(stbgfx, GFX_FB_OSD_FILLBLT, &fblt);
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

	return ioctl(stbgfx, GFX_FB_OSD_BITBLT, &fblt);
}

void
osd_draw_pixel_ayuv(osd_surface_t *surface, int x, int y, unsigned char a,
		    unsigned char Y, unsigned char U, unsigned char V)
{
	int offset;
	unsigned int line, remainder;

	if ((x >= surface->sfc.width) || (y >= surface->sfc.height))
		return;

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
}

void
osd_draw_pixel(osd_surface_t *surface, int x, int y, unsigned int c)
{
	unsigned char r, g, b, a, Y, U, V;

	c2rgba(c, &r, &g, &b, &a);
	rgb2yuv(r, g, b, &Y, &U, &V);

	osd_draw_pixel_ayuv(surface, x, y, a, Y, U, V);
}

unsigned int
osd_read_pixel(osd_surface_t *surface, int x, int y)
{
	int offset;
	unsigned char r, g, b, a, Y, U, V;
	unsigned int line, remainder;

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

void
osd_draw_horz_line(osd_surface_t *surface, int x1, int x2, int y,
		   unsigned int c)
{
	osd_fillblt(surface, x1, y, x2-x1+1, 1, c);
}

void
osd_draw_vert_line(osd_surface_t *surface, int x, int y1, int y2,
		   unsigned int c)
{
	osd_fillblt(surface, x, y1, 1, y2-y1+1, c);
}

void
osd_fill_rect(osd_surface_t *surface, int x, int y, int w, int h,
	      unsigned int c)
{
	osd_fillblt(surface, x, y, w, h, c);
}

void
osd_blit(osd_surface_t *dstsfc, int dstx, int dsty,
	 osd_surface_t *srcsfc, int srcx, int srcy, int w, int h)
{
	osd_bitblt(dstsfc, dstx, dsty, srcsfc, srcx, srcy, w, h);
}

void
osd_drawtext(osd_surface_t *surface, int x, int y, const char *str,
	     unsigned int fg, unsigned int bg, int background, void *FONT)
{
	osd_font_t *font = FONT;
	int h, n, i;
	int Y, X, cx, swidth;

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
}
