#ifndef SURFACE_H
#define SURFACE_H

/*
 * $Id$
 *
 * Jon Gettler <gettler@acm.org>
 */

typedef struct {
	unsigned long num;
	unsigned long unknown[4];
	unsigned long width;
	unsigned long height;
	char unknown2;
} stbgfx_display_t;

typedef struct {
	unsigned long handle;	/* surface handle */
	unsigned long width;
	unsigned long height;
	unsigned long flags;
	unsigned long unknown;
	unsigned long depth;	/* number of subplanes */
} stbgfx_sfc_t;

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

typedef struct {
	stbgfx_map_item_t map[3];
	unsigned long other[2];
} stbgfx_map_t;

struct osd_surface_s {
	stbgfx_display_t display;
	stbgfx_map_t map;
	stbgfx_sfc_t sfc;
	unsigned char *base[3];
};

#define GFX_FB_SFC_ALLOC	_IOWR(0xfb,1,int)
#define GFX_FB_SFC_FREE		_IOWR(0xfb,2,int*)
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
#define GFX_FB_SET_DISPLAY	_IOW(0xfb,14,int)
#define GFX_FB_OSD_CUR_MOVE_1	_IOW(0xfb,15,int*)
#define GFX_FB_OSD_CUR_MOVE_2	_IOW(0xfb,16,int)
#define GFX_FB_SET_OSD		_IOW(0xfb,18,int)
#define GFX_FB_SET_DISP_CTRL	_IOW(0xfb,21,int*)
#define GFX_FB_GET_DISP_CTRL	_IOWR(0xfb,22,int*)
#define GFX_FB_SET_VIS_DEV_CTL	_IOWR(0xfb,23,int*)
#define GFX_FB_GET_VIS_DEV_CTL	_IOWR(0xfb,24,int*)
#define GFX_FB_OSD_BITBLT	_IOW(0xfb,51,osd_bitblt_t*)
#define GFX_FB_OSD_FILLBLT	_IOW(0xfb,53,osd_fillblt_t*)
#define GFX_FB_OSD_ADVFILLBLT	_IOW(0xfb,54,osd_afillblt_t*)
#define GFX_FB_OSD_BLEND	_IOW(0xfb,55,int*)
#define GFX_FB_OSD_ADVBLEND	_IOW(0xfb,56,int*)
#define GFX_FB_OSD_RESIZE	_IOW(0xfb,58,int*)
#define GFX_FB_ENGINE_WAIT	_IOW(0xfb,60,int)
#define GFX_FB_RESET_ENGINE	_IO(0xfb,61)
#define GFX_FB_SET_ENGINE_MODE	_IOW(0xfb,62,int)
#define GFX_FB_GET_ENGINE_MODE	_IO(0xfb,63)
#define GFX_FB_SET_SOMETHING	_IO(0xfb,64)
#define GFX_FB_OSD_SFC_CLIP	_IOW(0xfb,65,osd_clip_rec_t*)
#define GFX_FB_OSD_COLOURKEY	_IOW(0xfb,67,int*)
#define GFX_FB_GET_SFC_PSEUDO	_IOWR(0xfb,68,int*)
extern int stbgfx;

#endif /* SURFACE_H */
