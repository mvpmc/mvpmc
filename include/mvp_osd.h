/*
 *  $Id$
 *
 *  Copyright (C) 2004, Jon Gettler
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

#ifndef MVP_OSD_H
#define MVP_OSD_H

typedef struct osd_surface_s osd_surface_t;

/*
 * surface functions
 */
extern osd_surface_t *osd_create_surface(int w, int h);
extern int osd_destroy_surface(osd_surface_t *surface);
extern void osd_display_surface(osd_surface_t *surface);
extern void osd_display_all_surfaces(void);
extern int osd_get_surface_size(osd_surface_t *surface, int *w, int *h);
extern void osd_set_surface_size(int w, int h);
extern int osd_close(void);

/*
 * drawing primitives
 */
extern void osd_draw_pixel(osd_surface_t *surface, int x, int y,
			   unsigned int c);
extern void osd_draw_horz_line(osd_surface_t *surface, int x1, int x2, int y,
			       unsigned int c);
extern void osd_draw_vert_line(osd_surface_t *surface, int x, int y1, int y2,
			       unsigned int c);
extern void osd_fill_rect(osd_surface_t *surface, int x, int y, int w, int h,
			  unsigned int c);
extern void osd_drawtext(osd_surface_t *surface, int x, int y, const char *str,
			 unsigned int fg, unsigned int bg, 
			 int background, void *FONT);
extern void osd_blit(osd_surface_t *dstsfc, int dstx, int dsty,
		     osd_surface_t *srcsfc, int srcx, int srcy, int w, int h);
extern unsigned int osd_read_pixel(osd_surface_t *surface, int x, int y);

extern int osd_display_bmp(void);

static inline unsigned long
osd_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	return (a<<24) | (r<<16) | (g<<8) | b;
}

#endif /* MVP_OSD_H */
