/*
 *  Copyright (C) 1997, 1998 Olivetti & Oracle Research Laboratory
 *  Portions Copyright (c) 2002 by Koninklijke Philips Electronics N.V.
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 *
 *  Microwindows interface by George Harvey
 *
 *  07/03/00  GH	created nanox.c to replace x.c, development
 *			being done using Microwindows 0.88pre3
 *  16/03/00  GH	try to match the VNC palette to the current
 *			palette using a lookup table
 *  06/05/00  GH	update for mwin 0.88pre7, use GrSetSystemPalette()
 *			instead of lookup table
 *  27/05/00  GH	update for mwin 0.88pre8
 *  03/06/00  GH	remove colour lookup code
 */

/*
 * nanox.c - functions to deal with nano-X display.
 */

#include <vncviewer.h>
#include <mvp_widget.h>
#include <unistd.h>

#define VW_WIDTH	1024	/* VNC window width */
#define VW_HEIGHT	768	/* VNC window height */
#define VW_X		0	/* VNC window origin */
#define VW_Y		0	/* VNC window origin */

#define SCROLLBAR_SIZE 10
#define SCROLLBAR_BG_SIZE (SCROLLBAR_SIZE + 2)

#define INVALID_PIXEL 0xffffffff
#define COLORMAP_SIZE 256

/* return 8 bit r, g or b component of 3/3/2 8 bit pixelval*/
#define PIXEL332RED8(pixelval)          ( (pixelval)       & 0xe0)
#define PIXEL332GREEN8(pixelval)        (((pixelval) << 3) & 0xe0)
#define PIXEL332BLUE8(pixelval)         (((pixelval) << 6) & 0xc0)
/* return 8 bit b, g or r component of 2/3/3 8 bit pixelval*/
#define PIXEL233BLUE8(pixelval)         ( (pixelval)       & 0xc0)
#define PIXEL233GREEN8(pixelval)        (((pixelval) << 2) & 0xe0)
#define PIXEL233RED8(pixelval)          (((pixelval) << 5) & 0xe0)
/* return 5/6/5 bit r, g or b component of 16 bit pixelval*/
#define PIXEL565RED(pixelval)           (((pixelval) >> 11) & 0x1f)
#define PIXEL565GREEN(pixelval)         (((pixelval) >> 5) & 0x3f)
#define PIXEL565BLUE(pixelval)          ((pixelval) & 0x1f)
/* create 32 bit 8/8/8/8 format pixel (0xAARRGGBB) from RGB triplet */
#define RGB2PIXEL8888(r,g,b)            (0xFF000000UL | ((r) << 16) | ((g) << 8) | (b))
/* create 24 bit 8/8/8 format pixel from RGB triplet */
#define RGB2PIXEL888(r,g,b)            (((r) << 16) | ((g) << 8) | (b))
/* create 16 bit 5/6/5 format pixel from RGB triplet */
#define RGB2PIXEL565(r,g,b)             ((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | (((b) & 0xf8) >> 3))
/* create 16 bit 5/5/5 format pixel from RGB triplet */
#define RGB2PIXEL555(r,g,b)             ((((r) & 0xf8) << 7) | (((g) & 0xf8) << 2) | (((b) & 0xf8) >> 3))
/* create 8 bit 3/3/2 format pixel from RGB triplet */
#define RGB2PIXEL332(r,g,b)             (((r) & 0xe0) | (((g) & 0xe0) >> 3) | (((b) & 0xc0) >> 6))
/* create 32 bit 8/8/8/8 format pixel (0xAARRGBB) from 24 bit 8/8/8 format pixel */
#define PIXEL8882PIXEL8888(pixelval)	(0xFF000000UL | pixelval) 

/*
 * global data
 */
Colormap	cmap;
Display		*dpy;
Window		canvas;
GR_GC_ID	gc;
GR_GC_ID	srcGC;
GR_GC_ID	dstGC;

/* BGR233ToPixel array */
unsigned long BGR233ToPixel[COLORMAP_SIZE] = { \
	0x00, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, \
	0xf7, 0xc7, 0x87, 0x47, 0x07, 0xc6, 0x86, 0x46, \
	0xf7, 0xc7, 0x87, 0x47, 0x07, 0xc6, 0x86, 0x46, \
	0xf7, 0xc7, 0x87, 0x47, 0x07, 0xc6, 0x86, 0x46, \
	0xf7, 0xc7, 0x87, 0x47, 0x07, 0xc6, 0x86, 0x46, \
	0xf7, 0xc7, 0x87, 0x47, 0x07, 0xc6, 0x86, 0x46, \
	0xf7, 0xc7, 0x87, 0x47, 0x07, 0xc6, 0x86, 0x46, \
	0xf7, 0xc7, 0x87, 0x47, 0x07, 0xc6, 0x86, 0x46, \
	0xf7, 0xc7, 0x87, 0x47, 0x07, 0xc6, 0x86, 0x46, \
	0x0c, 0x4c, 0x8c, 0xcc, 0x0d, 0x4d, 0x8d, 0xcd, \
	0xcb, 0x80 \
	};

/* colour palette for 8-bit displays */
static GR_PALETTE srv_pal;	/* VNC server palette */

/* temporary keyboard mapping array */
/* ^ (0x5e) = up (0xff52), < = left (0xff51), > = right (0xff53), V (0x56) = down (0xff54)
 */
CARD32 kmap[] = {0xff51, 0xff53, 0xff52, 0xff54, 0x04, 0x05, 0x06, 0x07, \
		0x08, 0x09, 0x0d, 0x0b, 0x0c, 0x0a, 0x0e, 0x0f, \
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, \
		0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0xff52, \
		0xff54, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, \
		0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, \
		'0', '1', '2', '3', '4', '5', '6', '7', \
		'8', '9', 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, \
		0x40, 'A', 'B', 'C', 'D', 'E', 'F', 'G', \
		'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', \
		'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', \
		'X', 'Y', 'Z', 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, \
		0x60, 'a', 'b', 'c', 'd', 'e', 'f', 'g', \
		'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', \
		'p', 'q', 'r', 's', 't', 'u', 'v', 'w', \
		'x', 'y', 'z', 0x7b, 0x7c, 0x7d, 0x7e, 0x7f };

//static Display		nx_dpy;
//static GR_WINDOW_ID	wid;
//static int		pixtype;	/* format of pixel value */

extern MWPIXELVAL gr_foreground;	/* for debugging only */
extern mvp_widget_t *vnc_widget;
/*
 * Initialize graphics and open a window for the viewer
 */
Bool
CreateRFBWindow(void)
{
	return True;
}

/*
 * set the server palette to the requested colour
 * NOTE: this has only been tested for 8-bit colour!
 */
int
RFBStoreColor(Display *dpy, Colormap cmap, XColor *xc)
{
	unsigned char ind;

	ind = xc->pixel & 0xff;		/* colour map index */
	/*
	 * the colours are passed as 16-bit values so divide by 256 to
	 * get 8-bit RGB values
	 */
	srv_pal.palette[0].r = (xc->red / 256) & 0xff;
	srv_pal.palette[0].g = (xc->green / 256) & 0xff;
	srv_pal.palette[0].b = (xc->blue / 256) & 0xff;
	srv_pal.count = 1;
#if 0
	/* DEBUG */
	printf("XStoreColor: ind=%d, r=%02x, g=%02x, b=%02x\n", ind, \
		srv_pal.palette[0].r, srv_pal.palette[0].g, \
		srv_pal.palette[0].b);
#endif
	GrSetSystemPalette(ind, &srv_pal);

	return(0);
}

/*
 * Copy a rectangular block of pixels
 */
int
RFBCopyArea(Display *dpy, Window src, Window dst, GR_GC_ID gc,
        int x1, int y1, int w, int h, int x2, int y2)
{
	mvpw_copy_area(vnc_widget, x2, y2, src, x1, y1, w, h);
	return(0);
}

/*
 * Fill a rectangular block
 */
int
RFBFillRectangle(Display *dpy, Window canvas, GR_GC_ID gc,
        int x, int y, int w, int h)
{
	mvpw_fill_rect(vnc_widget, x, y, w, h, NULL);
	
	return(0);
}

/*
 * get the X display name
 */
char *
RFBDisplayName(char *display)
{
	return((char *)NULL);
}

/*
 * Change the graphics context.
 * VNC only uses this to set the foreground colour.
 */
int
RFBChangeGC(Display *dpy, GR_GC_ID gc, unsigned long vmask, GR_GC_INFO *gcv)
{
	mvpw_surface_attr_t surface;
	//MWPIXELVAL fg1;
	MWPIXELVAL fg;
	
	mvpw_get_surface_attr(vnc_widget, &surface);
        
        /* all we need is the foreground colour */
	memcpy(&fg, &gcv->foreground, sizeof(gcv->foreground));
//	if (surface.pixtype != MWPF_TRUECOLOR332 || useBGR233 ) {
//		ConvertData((CARD8 *) &fg, (CARD8 *) &fg, 1, 1, surface.pixtype);
//	}
	if(surface.pixtype == MWPF_TRUECOLOR8888) {
		fg = PIXEL8882PIXEL8888(fg);
	}
	surface.foreground = fg;
	mvpw_set_surface_attr(vnc_widget, &surface);
	
        return(0);
}

/*
 * Ring the bell.
 */
int
RFBBell(Display *dpy, int pc)
{
        return(0);
}

/*
 *
 */
int
RFBSync(Display *dpy, Bool disc)
{
        return(0);
}

/*
 *
 */
int
RFBSelectInput(Display *dpy, Window win, long evmask)
{
        return(0);
}

/*
 *
 */
int
RFBStoreBytes(Display *dpy, char *bytes, int nbytes)
{
        return(0);
}

/*
 *
 */
int
RFBSetSelectionOwner(Display *dpy, Atom sel, Window own, Time t)
{
        return(0);
}

void
ConvertData(CARD8 *buf, CARD8 *buf2, int width, int height, int pixtype)
{
        int i,j,k,r,g,b;
        MWPIXELVAL p;
        MWPIXELVAL c;
	rfbPixelFormat *format;

	format = &rfbsi.format;
	
	for(i=0 ; i < width ; i++){
		for(j=0 ; j < height ; j++) {
			k = ((i * height) + j) * sizeof(MWPIXELVAL);
			memcpy(&c, buf + k, sizeof(MWPIXELVAL));
//	printf("PixelVal = 0x%08x bpp=%i\n", c, format->bitsPerPixel);
			if (useBGR233) {
				r = PIXEL233RED8(c);
				g = PIXEL233GREEN8(c);
				b = PIXEL233BLUE8(c);
			} else {
				r = PIXEL332RED8(c);
				g = PIXEL332GREEN8(c);
				b = PIXEL332BLUE8(c);
			}
			switch (pixtype) {
			case MWPF_TRUECOLOR8888:
				p = RGB2PIXEL8888(r,g,b);
				break;
			case MWPF_TRUECOLOR0888:
				p = RGB2PIXEL888(r,g,b);
				break;
			case MWPF_TRUECOLOR565:
				p = RGB2PIXEL565(r,g,b);
				break;
			case MWPF_TRUECOLOR555:
				p = RGB2PIXEL555(r,g,b);
				break;
			case MWPF_TRUECOLOR332:
				p = RGB2PIXEL332(r,g,b);
				break;
			default: 
				printf("Unknown pixtype %i\n", pixtype);
				break;
			}
			memcpy(buf2 + k, &p, sizeof(MWPIXELVAL));
		}
	}
}

/*
 * Copy raw pixel data to the screen
 */
void
CopyDataToScreen(CARD8 *buf, int x, int y, int width, int height)
{
	mvpw_surface_attr_t surface;
	
	mvpw_get_surface_attr(vnc_widget, &surface);
			
	if (rawDelay != 0) {
		usleep(rawDelay * 1000);
	}
	if (surface.pixtype == MWPF_TRUECOLOR332 && useBGR233 ) {
        	char buf2[width*height*sizeof(MWPIXELVAL)];
		//printf("CopyDataToScreen - BGR233 Convertion - x=%i, y=%i, w=%i, h=%i\n", x, y, width, height);
		ConvertData(buf, (CARD8 *) buf2, width, height, surface.pixtype);
		mvpw_set_surface(vnc_widget, buf2, x, y, width, height);
	} else {
        	//printf("CopyDataToScreen - No conversion - x=%i, y=%i, w=%i, h=%i\n", x, y, width, height);
		mvpw_set_surface(vnc_widget, buf, x, y, width, height);
	}
}

/*
 * Close everything down before exiting.
 */
void
ShutdownRFB(void)
{
}

