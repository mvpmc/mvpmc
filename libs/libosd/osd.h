#ifndef OSD_H
#define OSD_H

/*
 * $Id$
 *
 * Jon Gettler <gettler@acm.org>
 */

typedef struct {
	unsigned long handle;
	unsigned long x;
	unsigned long y;
	unsigned long width;
	unsigned long height;
	unsigned long colour;
} osd_fillblt_t; 

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

typedef struct {
	unsigned long dst_handle;
	unsigned long dst_x;
	unsigned long dst_y;
	unsigned long width;
	unsigned long height;
	unsigned long src_handle;
	unsigned long src_x;
	unsigned long src_y;
	unsigned long u1;
	unsigned long u2;
	unsigned char u3;
} osd_bitblt_t; 

typedef struct {
	unsigned long handle;
	unsigned long left;
	unsigned long top;
	unsigned long right;
	unsigned long bottom;
} osd_clip_rec_t; 

extern void rgb2yuv(unsigned char r, unsigned char g, unsigned char b,
		    unsigned char *y, unsigned char *u, unsigned char *v);
extern void yuv2rgb(unsigned char y, unsigned char u, unsigned char v,
		    unsigned char *r, unsigned char *g, unsigned char *b);
extern void gfx_init(void);

#endif /* STBGFX_H */
