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

#ifndef OSD_CURSOR_H
#define OSD_CURSOR_H

#define CURSOR_MAX_COLORS	16

typedef struct {
	unsigned char alpha;
	unsigned char red;
	unsigned char green;
	unsigned char blue;
} cursor_palette_t;

typedef struct {
	int handle;
	unsigned int start;
	unsigned int count;
	cursor_palette_t *palette;
} cursor_palette_data_t;

typedef struct {
	stbgfx_display_t display;
	stbgfx_map_t map;
	stbgfx_sfc_t sfc;
	unsigned char *base;
	int ncolors;
	cursor_palette_t colors[CURSOR_MAX_COLORS];
} cursor_data_t;

extern osd_surface_t* cursor_create(int w, int h, unsigned long color);
extern void cursor_init(void);

#endif /* OSD_CURSOR_H */
