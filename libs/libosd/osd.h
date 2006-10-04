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

#define gfx_init		__osd_gfx_init

/**
 * Fill BLT structure.
 */
typedef struct {
	unsigned long handle;		/**< surface handle */
	unsigned long x;		/**< horizontal coordinate */
	unsigned long y;		/**< vertical coordinate */
	unsigned long width;		/**< fill width */
	unsigned long height;		/**< fill height */
	unsigned long colour;		/**< RGB color */
} osd_fillblt_t; 

/**
 * OSD blend.
 */
typedef struct {
	unsigned long handle1;
	unsigned long x;
	unsigned long y;
	unsigned long w;
	unsigned long h;
	unsigned long handle2;
	unsigned long x1;
	unsigned long y1;
	unsigned long w1;
	unsigned long h1;
	unsigned long colour1;
} osd_blend_t;

/**
 * Advanced fill.
 */
typedef struct {
	unsigned long handle;
	unsigned long x;
	unsigned long y;
	unsigned long w;
	unsigned long h;
	unsigned long colour1;
	unsigned long colour2;
	unsigned long colour3;
	unsigned long colour4;
	unsigned long colour5;
	unsigned long colour6;
	unsigned long colour7;
	unsigned char c[2];
} osd_afillblt_t;

/**
 * Bit Block Transfer.
 */
typedef struct {
	unsigned long dst_handle;	/**< destination surface handle */
	unsigned long dst_x;		/**< destination horizontal */
	unsigned long dst_y;		/**< destination vertical */
	unsigned long width;		/**< block width */
	unsigned long height;		/**< block height */
	unsigned long src_handle;	/**< source surface handle */
	unsigned long src_x;		/**< source horizontal */
	unsigned long src_y;		/**< source vertical */
	unsigned long u1;
	unsigned long u2;
	unsigned char u3;
} osd_bitblt_t; 

/**
 * Clip Rectangle.
 */
typedef struct {
	unsigned long handle;
	unsigned long left;
	unsigned long top;
	unsigned long right;
	unsigned long bottom;
} osd_clip_rec_t; 

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

/**
 * Initialize the graphics device.
 */
extern void gfx_init(void);

/*
 * XXX: The following functions have not been tested!
 */
extern int osd_blend(osd_surface_t *surface, int x, int y, int w, int h,
		     osd_surface_t *surface2, int x2, int y2, int w2, int h2,
		     unsigned long colour);
extern int osd_afillblt(osd_surface_t *surface,
			int x, int y, int w, int h, unsigned long colour);
extern int osd_sfc_clip(osd_surface_t *surface,
			int left, int top, int right, int bottom);
extern int osd_get_visual_device_control(osd_surface_t *surface);
extern int osd_cur_set_attr(osd_surface_t *surface, int x, int y);
extern int move_cursor(osd_surface_t *surface, int x, int y);
extern int osd_get_engine_mode(osd_surface_t *surface);
extern int set_engine_mode(osd_surface_t *surface, int mode);
extern int osd_reset_engine(osd_surface_t *surface);
extern int osd_set_disp_ctrl(osd_surface_t *surface);
extern int osd_get_disp_ctrl(osd_surface_t *surface);

#endif /* STBGFX_H */
