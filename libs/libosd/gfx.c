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
gfx_fill_rect(osd_surface_t *surface, int x, int y, int width, int height, 
	      unsigned int c)
{
	gfx_fillblt_t fblt;

	fblt.handle = surface->data.gfx.sfc.handle;
	fblt.x = x;
	fblt.y = y;
	fblt.width = width;
	fblt.height = height;
	fblt.colour = c;

	return ioctl(surface->fd, GFX_FB_OSD_FILLBLT, &fblt);
}

static int
gfx_bitblt(osd_surface_t *dstsfc, int xd, int yd,
	   osd_surface_t *srcsfc, int x, int y, int width, int height)
{
	gfx_bitblt_t fblt;

	memset(&fblt, 0, sizeof(fblt));

	fblt.dst_handle = dstsfc->data.gfx.sfc.handle;
	fblt.dst_x = xd;
	fblt.dst_y = yd;

	fblt.src_handle = srcsfc->data.gfx.sfc.handle;
	fblt.src_x = x;
	fblt.src_y = y;

	fblt.width = width;
	fblt.height = height;

	fblt.u1 = 1;
	fblt.u2 = 0;
	fblt.u3 = 0x0f;

	return ioctl(dstsfc->fd, GFX_FB_OSD_BITBLT, &fblt);
}

static int
gfx_draw_pixel_ayuv(osd_surface_t *surface, int x, int y, unsigned char a,
		    unsigned char Y, unsigned char U, unsigned char V)
{
	int offset;
	unsigned int line, remainder;

	if ((x >= surface->data.gfx.sfc.width) ||
	    (y >= surface->data.gfx.sfc.height))
		return -1;

	remainder = (surface->data.gfx.sfc.width % 4);
	if (remainder == 0)
		line = surface->data.gfx.sfc.width;
	else
		line = surface->data.gfx.sfc.width + (4 - remainder);

	offset = (y * line) + x;

	*(surface->data.gfx.base[0] + offset) = Y;
	*(surface->data.gfx.base[1] + (offset & 0xfffffffe)) = U;
	*(surface->data.gfx.base[1] + (offset & 0xfffffffe) + 1) = V;
	*(surface->data.gfx.base[2] + offset) = a;

	return 0;
}

static int
gfx_draw_pixel(osd_surface_t *surface, int x, int y, unsigned int c)
{
	unsigned char r, g, b, a, Y, U, V;

	c2rgba(c, &r, &g, &b, &a);
	rgb2yuv(r, g, b, &Y, &U, &V);

	return osd_draw_pixel_ayuv(surface, x, y, a, Y, U, V);
}

static int
gfx_destroy_surface(osd_surface_t *surface)
{
	int i;
	int fd = surface->fd;

	for (i=0; i<3; i++)
		if (surface->data.gfx.base[i])
			munmap(surface->data.gfx.base[i],
			       surface->data.gfx.map.map[i].size);

	if (ioctl(fd, GFX_FB_SFC_FREE, surface->data.gfx.sfc.handle) < 0)
		return -1;

	return 0;
}

static int
gfx_display_surface(osd_surface_t *surface)
{
	unsigned long fb_descriptor[2];
	int fd = surface->fd;

	fb_descriptor[0] = surface->data.gfx.sfc.handle;

	fb_descriptor[1] = 1; 
	
	if (ioctl(fd, GFX_FB_ATTACH, fb_descriptor) < 0)
		return -1;

	visible = surface;

	return 0;
}

static int
gfx_undisplay_surface(osd_surface_t *surface)
{
	unsigned long fb_descriptor[2];
	int fd = surface->fd;

	fb_descriptor[0] = surface->data.gfx.sfc.handle;
	fb_descriptor[1] = 1; 
	
	ioctl(fd, GFX_FB_SFC_DETACH, fb_descriptor);

	visible = NULL;

	return 0;
}

static unsigned int
gfx_read_pixel(osd_surface_t *surface, int x, int y)
{
	int offset;
	unsigned char r, g, b, a, Y, U, V;
	unsigned int line, remainder;

	if ((x >= surface->data.gfx.sfc.width) ||
	    (y >= surface->data.gfx.sfc.height))
		return 0;

	remainder = (surface->data.gfx.sfc.width % 4);
	if (remainder == 0)
		line = surface->data.gfx.sfc.width;
	else
		line = surface->data.gfx.sfc.width + (4 - remainder);

	offset = (y * line) + x;

	Y = *(surface->data.gfx.base[0] + offset);
	U = *(surface->data.gfx.base[1] + (offset & 0xfffffffe));
	V = *(surface->data.gfx.base[1] + (offset & 0xfffffffe) + 1);
	a = *(surface->data.gfx.base[2] + offset);

	yuv2rgb(Y, U, V, &r, &g, &b);

	PRINTF("YUVa: %d %d %d %d  RGB: %d %d %d\n", Y, U, V, a, r, g, b);

	return (a << 24) | (r << 16) | (g << 8) | b;
}

static int
gfx_draw_horz_line(osd_surface_t *surface, int x1, int x2, int y,
		   unsigned int c)
{
	return gfx_fill_rect(surface, x1, y, x2-x1+1, 1, c);
}

static int
gfx_draw_vert_line(osd_surface_t *surface, int x, int y1, int y2,
		   unsigned int c)
{
	return gfx_fill_rect(surface, x, y1, 1, y2-y1+1, c);
}

static int
gfx_blit(osd_surface_t *dstsfc, int dstx, int dsty,
	 osd_surface_t *srcsfc, int srcx, int srcy, int w, int h)
{
	return gfx_bitblt(dstsfc, dstx, dsty, srcsfc, srcx, srcy, w, h);
}

/*
 * XXX: this has not been tested!
 */
static int
gfx_blend(osd_surface_t *surface, int x, int y, int w, int h,
	  osd_surface_t *surface2, int x2, int y2, int w2, int h2,
	  unsigned long colour)
{
	gfx_blend_t fblt;

	memset(&fblt, 0, sizeof(fblt));

	fblt.handle1 = surface->data.gfx.sfc.handle;
	fblt.x = x;
	fblt.y = y;
	fblt.w = w;
	fblt.h = h;

	fblt.handle2 = surface2->data.gfx.sfc.handle;
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
static int
gfx_afillblt(osd_surface_t *surface,
	     int x, int y, int w, int h, unsigned long colour)
{
	gfx_afillblt_t fblt;

	fblt.handle = surface->data.gfx.sfc.handle;
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
static int
gfx_clip(osd_surface_t *surface, int left, int top, int right, int bottom)
{
	gfx_clip_rec_t rec;

	rec.handle = surface->data.gfx.sfc.handle;
	rec.left = left;
	rec.top = top;
	rec.bottom = bottom;
	rec.right = right;

	return ioctl(surface->fd, GFX_FB_OSD_SFC_CLIP, &rec);
}

/*
 * XXX: this has not been tested!
 */
static int
gfx_get_visual_device_control(osd_surface_t *surface)
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
static int
gfx_cur_set_attr(osd_surface_t *surface, int x, int y)
{
	unsigned long int data[3];

	data[0] = surface->data.gfx.sfc.handle;
	data[1] = x;
	data[2] = y;

	return ioctl(surface->fd, GFX_FB_OSD_CUR_SETATTR, data);
}

/*
 * XXX: this has not been tested!
 */
static int
gfx_get_engine_mode(osd_surface_t *surface)
{
	return ioctl(surface->fd, GFX_FB_GET_ENGINE_MODE);
}

/*
 * XXX: this has not been tested!
 */
static int
gfx_set_engine_mode(osd_surface_t *surface, int mode)
{
	return ioctl(surface->fd, GFX_FB_SET_ENGINE_MODE, mode);
}

/*
 * XXX: this has not been tested!
 */
static int
gfx_reset_engine(osd_surface_t *surface)
{
	return ioctl(surface->fd, GFX_FB_RESET_ENGINE);
}

/*
 * XXX: this has not been tested!
 */
static int
gfx_set_display_control(osd_surface_t *surface, int type, int value)
{
	gfx_display_control_t ctrl;

	ctrl.type = type;
	ctrl.value = value;

	return ioctl(surface->fd, GFX_FB_SET_DISP_CTRL, &ctrl);
}

/*
 * XXX: this has not been tested!
 */
static int
gfx_get_display_control(osd_surface_t *surface, int type)
{
	gfx_display_control_t ctrl;

	ctrl.type = type;
	ctrl.value = 0;

	if (ioctl(surface->fd, GFX_FB_GET_DISP_CTRL, &ctrl) < 0)
		return -1;
	return ctrl.value;
}

static int
gfx_get_display_options(osd_surface_t *surface)
{
	if (ioctl(surface->fd, GFX_FB_GET_DISPLAY, &surface->data.gfx.display) < 0)
		return -1;

	return surface->data.gfx.display.option;
}

static int
gfx_set_display_options(osd_surface_t *surface, unsigned char option)
{
	surface->data.gfx.display.option = option;

	return ioctl(surface->fd, GFX_FB_SET_DISPLAY, &surface->data.gfx.display);
}

osd_func_t fp_gfx = {
	.destroy = gfx_destroy_surface,
	.display = gfx_display_surface,
	.undisplay = gfx_undisplay_surface,
	.palette_init = NULL,
	.palette_add_color = NULL,
	.draw_pixel = gfx_draw_pixel,
	.draw_pixel_ayuv = gfx_draw_pixel_ayuv,
	.read_pixel = gfx_read_pixel,
	.draw_horz_line = gfx_draw_horz_line,
	.draw_vert_line = gfx_draw_vert_line,
	.fill_rect = gfx_fill_rect,
	.blit = gfx_blit,
	.draw_indexed_image = NULL,
	.blend = gfx_blend,
	.afillblt = gfx_afillblt,
	.clip = gfx_clip,
	.get_dev_control = gfx_get_visual_device_control,
	.set_attr = gfx_cur_set_attr,
	.get_engine_mode = gfx_get_engine_mode,
	.set_engine_mode = gfx_set_engine_mode,
	.reset_engine = gfx_reset_engine,
	.set_display_control = gfx_set_display_control,
	.get_display_control = gfx_get_display_control,
	.set_display_options = gfx_set_display_options,
	.get_display_options = gfx_get_display_options,
};

osd_surface_t*
gfx_create(int w, int h, unsigned long color)
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

	memset(&surface->data.gfx.sfc, 0, sizeof(surface->data.gfx.sfc));
	surface->data.gfx.sfc.width = w;
	surface->data.gfx.sfc.height = h;
	surface->data.gfx.sfc.background = color;

	surface->data.gfx.sfc.flags = 0x3f1533;
	surface->data.gfx.sfc.unknown = 1;

	if (ioctl(fd, GFX_FB_SFC_ALLOC, &surface->data.gfx.sfc) != 0) {
		goto err;
	}

	memset(&surface->data.gfx.map, 0, sizeof(surface->data.gfx.map));
	surface->data.gfx.map.map[0].unknown = surface->data.gfx.sfc.handle;
	if (ioctl(fd, GFX_FB_MAP, &surface->data.gfx.map) != 0)
		goto err;
	if ((surface->data.gfx.base[0]=mmap(NULL, surface->data.gfx.map.map[0].size,
				   PROT_READ|PROT_WRITE, MAP_SHARED, fd,
				   surface->data.gfx.map.map[0].addr)) == MAP_FAILED)
		goto err;
	if ((surface->data.gfx.base[1]=mmap(NULL, surface->data.gfx.map.map[1].size,
				   PROT_READ|PROT_WRITE, MAP_SHARED, fd,
				   surface->data.gfx.map.map[1].addr)) == MAP_FAILED)
		goto err;
	if ((surface->data.gfx.base[2]=mmap(NULL, surface->data.gfx.map.map[2].size,
				   PROT_READ|PROT_WRITE, MAP_SHARED, fd,
				   surface->data.gfx.map.map[2].addr)) == MAP_FAILED)
		goto err;

	surface->fd = fd;

	if (gfx_get_display_options(surface) < 0)
		goto err;

	PRINTF("surface 0x%.8x created of size %d x %d   [%d]\n",
	       surface, w, h, surface->data.gfx.map.map[0].size);

	i = 0;
	while ((all[i] != NULL) && (i < OSD_MAX_SURFACES))
		i++;
	if (i < OSD_MAX_SURFACES)
		all[i] = surface;

	surface->type = OSD_GFX;
	surface->fp = &fp_gfx;

	surface->width = w;
	surface->height = h;

	return surface;

 err:
	if (surface)
		free(surface);

	return NULL;
}

