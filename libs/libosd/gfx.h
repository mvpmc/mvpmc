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

#ifndef OSD_GFX_H
#define OSD_GFX_H

#define gfx_init		__osd_gfx_init

/**
 * STB Graphics Display.
 */
typedef struct {
	unsigned long num;
	unsigned long unknown[4];
	unsigned long width;		/**< display width */
	unsigned long height;		/**< display height */
	unsigned char option;
} stbgfx_display_t;

/**
 * STB Graphics Surface.
 */
typedef struct {
	unsigned long handle;		/**< surface handle */
	unsigned long width;		/**< surface width */
	unsigned long height;		/**< surface height */
	unsigned long flags;
	unsigned long unknown;
	unsigned long background;	/**< background color */
} stbgfx_sfc_t;

/**
 * Memory mapped STB graphics item.
 */
typedef struct {
	unsigned long unknown;
	unsigned long win_unknown;
	unsigned long addr;
	unsigned long size;
	unsigned long unknown2;
	unsigned long width;
	unsigned long height;
	unsigned long unknown3;
	unsigned long unknown4;
	unsigned long width2;
	unsigned long unknown5;
	unsigned long unknown6;
} stbgfx_map_item_t;

/**
 * STB graphics memory map.
 */
typedef struct {
	stbgfx_map_item_t map[3];
	unsigned long other[2];
} stbgfx_map_t;

#define GFX_FB_SFC_ALLOC	_IOWR(0xfb,1,int)
#define GFX_FB_SFC_FREE		_IOW(0xfb,2,int)
#define GFX_FB_MAP		_IOWR(0xfb,3,int)
#define GFX_FB_SFC_UNMAP	_IOWR(0xfb,4,int*)
#define GFX_FB_SET_PAL_1	_IOWR(0xfb,5,int*)
#define GFX_FB_SET_PAL_2	_IOW(0xfb,6,int*)
#define GFX_FB_OSD_SURFACE	_IO(0xfb,7)
#define GFX_FB_SFC_SET_SHARE	_IOW(0xfb,8,int)
#define GFX_FB_OSD_CUR_SETATTR	_IOW(0xfb,9,int*)
#define GFX_FB_ATTACH		_IOW(0xfb,11,int)
#define GFX_FB_SFC_DETACH	_IOW(0xfb,12,int*)
#define GFX_FB_MOVE_DISPLAY	_IOWR(0xfb,13,int)
#define GFX_FB_SET_DISPLAY	_IOW(0xfb,14,stbgfx_display_t*)
#define GFX_FB_OSD_CUR_MOVE_1	_IOW(0xfb,15,int*)
#define GFX_FB_OSD_CUR_MOVE_2	_IOW(0xfb,16,int)
#define GFX_FB_SET_OSD		_IOW(0xfb,18,int)
#define GFX_FB_SET_DISP_CTRL	_IOW(0xfb,21,gfx_display_control_t*)
#define GFX_FB_GET_DISP_CTRL	_IOWR(0xfb,22,int*)
#define GFX_FB_SET_VIS_DEV_CTL	_IOWR(0xfb,23,int*)
#define GFX_FB_GET_VIS_DEV_CTL	_IOWR(0xfb,24,int*)
#define GFX_FB_OSD_BITBLT	_IOW(0xfb,51,gfx_bitblt_t*)
#define GFX_FB_OSD_FILLBLT	_IOW(0xfb,53,gfx_fillblt_t*)
#define GFX_FB_OSD_ADVFILLBLT	_IOW(0xfb,54,gfx_afillblt_t*)
#define GFX_FB_OSD_BLEND	_IOW(0xfb,55,gfx_blend_t*)
#define GFX_FB_OSD_ADVBLEND	_IOW(0xfb,56,int*)
#define GFX_FB_OSD_RESIZE	_IOW(0xfb,58,int*)
#define GFX_FB_ENGINE_WAIT	_IOW(0xfb,60,int)
#define GFX_FB_RESET_ENGINE	_IO(0xfb,61)
#define GFX_FB_SET_ENGINE_MODE	_IOW(0xfb,62,int)
#define GFX_FB_GET_ENGINE_MODE	_IO(0xfb,63)
#define GFX_FB_GET_SFC_INFO	_IO(0xfb,64,int*)
#define GFX_FB_OSD_SFC_CLIP	_IOW(0xfb,65,gfx_clip_rec_t*)
#define GFX_FB_OSD_COLOURKEY	_IOW(0xfb,67,int*)
#define GFX_FB_GET_SFC_PSEUDO	_IOWR(0xfb,68,int*)

#define stbgfx		__osd_stbgfx

extern int stbgfx;

typedef struct {
	stbgfx_display_t display;
	stbgfx_map_t map;
	stbgfx_sfc_t sfc;
	unsigned char *base[3];
} gfx_data_t;

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
} gfx_fillblt_t; 

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
} gfx_blend_t;

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
} gfx_afillblt_t;

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
} gfx_bitblt_t; 

/**
 * Clip Rectangle.
 */
typedef struct {
	unsigned long handle;
	unsigned long left;
	unsigned long top;
	unsigned long right;
	unsigned long bottom;
} gfx_clip_rec_t; 

/**
 * Display Control.
 */
typedef struct {
	int type;
	int value;
} gfx_display_control_t;

osd_surface_t* gfx_create(int w, int h, unsigned long color, osd_type_t type);

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
extern int osd_move_cursor(osd_surface_t *surface, int x, int y);
extern int osd_get_engine_mode(osd_surface_t *surface);
extern int osd_reset_engine(osd_surface_t *surface);

#endif /* OSD_GFX_H */
