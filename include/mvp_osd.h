/*
 *  Copyright (C) 2004-2006, Jon Gettler
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

/** \file mvp_osd.h
 * MediaMVP On-Screen-Display interface library.  This library is used to
 * draw graphics on the TV screen.
 */

#ifndef MVP_OSD_H
#define MVP_OSD_H

typedef struct osd_surface_s osd_surface_t;

/**
 * Create a new drawing surface
 * \param w surface width (-1 for full width)
 * \param h surface height (-1 for full height)
 * \return handle to the new surface
 */
extern osd_surface_t *osd_create_surface(int w, int h);

/**
 * Destroy a drawing surface.
 * \param surface handle to a drawing surface
 * \retval 0 success
 * \retval -1 error
 */
extern int osd_destroy_surface(osd_surface_t *surface);

/**
 * Destroy all existing drawing surfaces.
 */
extern void osd_destroy_all_surfaces(void);

/**
 * Display a drawing surface.
 * \param surface drawing surface to display
 */
extern void osd_display_surface(osd_surface_t *surface);

/**
 * Return the size of the drawing surface
 * \param surface handle to a drawing surface
 * \param[out] w surface width
 * \param[out] h surface height
 * \retval 0 success
 * \retval -1 error
 */
extern int osd_get_surface_size(osd_surface_t *surface, int *w, int *h);

/**
 * Set the full size of the screen.
 * \param w screen width
 * \param h screen height
 */
extern void osd_set_screen_size(int w, int h);

/**
 * Shut down access to the hardware OSD device.
 * \retval 0 success
 * \retval -1 error
 */
extern int osd_close(void);

/**
 * Draw a single pixel on a drawing surface.
 * \param surface handle to a drawing surface
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param c color
 */
extern void osd_draw_pixel(osd_surface_t *surface, int x, int y,
			   unsigned int c);

/**
 * Draw a single pixel on a drawing surface.
 * \param surface handle to a drawing surface
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param a alpha channel
 * \param Y y channel
 * \param U u channel
 * \param V v channel
 */
extern void osd_draw_pixel_ayuv(osd_surface_t *surface, int x, int y,
				unsigned char a, unsigned char Y,
				unsigned char U, unsigned char V);

/**
 * Draw a horizontal line on a drawing surface.
 * \param surface handle to a drawing surface
 * \param x1 horizontal coordinate of start of line
 * \param x2 horizontal coordinate of end of line
 * \param y vertical coordinate
 * \param c color
 */
extern void osd_draw_horz_line(osd_surface_t *surface, int x1, int x2, int y,
			       unsigned int c);

/**
 * Draw a vertical line on a drawing surface.
 * \param surface handle to a drawing surface
 * \param x horizontal coordinate
 * \param y1 vertical coordinate of start of line
 * \param y2 vertical coordinate of end of line
 * \param c color
 */
extern void osd_draw_vert_line(osd_surface_t *surface, int x, int y1, int y2,
			       unsigned int c);

/**
 * Fill a rectangle on a drawing surface.
 * \param surface handle to a drawing surface
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param w width of rectangle
 * \param h height of rectangle
 * \param c color
 */
extern void osd_fill_rect(osd_surface_t *surface, int x, int y, int w, int h,
			  unsigned int c);

/**
 * Draw a text string on a drawing surface.
 * \param surface handle to a drawing surface
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param str text string to draw
 * \param fg foreground text color
 * \param bg background text color
 * \param background 0 if background should not be drawn, 1 if it should
 * \param FONT font to use
 */
extern void osd_drawtext(osd_surface_t *surface, int x, int y, const char *str,
			 unsigned int fg, unsigned int bg, 
			 int background, void *FONT);

/**
 * Bit blast a rectangle from one drawing surface to another.
 * \param dstsfc handle to the destination drawing surface
 * \param dstx destination horizontal coordinate
 * \param dsty destination vertical coordinate
 * \param srcsfc handle to the source drawing surface
 * \param srcx source horizontal coordinate
 * \param srcy source vertical coordinate
 * \param w width of rectangle
 * \param h height of rectangle
 */
extern void osd_blit(osd_surface_t *dstsfc, int dstx, int dsty,
		     osd_surface_t *srcsfc, int srcx, int srcy, int w, int h);

/**
 * Return the color of a specified pixel.
 * \param surface handle to a drawing surface
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \return pixel color
 */
extern unsigned int osd_read_pixel(osd_surface_t *surface, int x, int y);

/**
 * Convert RGBA into a pixel color.
 * \param r red
 * \param g green
 * \param b blue
 * \param a alpha channel
 * \return pixel color
 */
static inline unsigned long
osd_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	return (a<<24) | (r<<16) | (g<<8) | b;
}

#endif /* MVP_OSD_H */

#ifndef FONT_H
#define FONT_H

/*
 * This structure is for compatibility with bogl fonts.  Use bdftobogl to
 * create new .c font files from X11 BDF fonts.
 */
typedef struct bogl_font {
	char *name;			/* Font name. */
	int height;			/* Height in pixels. */
	unsigned long *content;		/* 32-bit right-padded bitmap array. */
	short *offset;			/* 256 offsets into content. */
	unsigned char *width;		/* 256 character widths. */
} osd_font_t;

#endif /* FONT_H */
