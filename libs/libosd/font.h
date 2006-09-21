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
