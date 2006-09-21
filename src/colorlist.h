/*
 *  Copyright (C) 2004-2006, John Honeycutt
 *  http://www.mvpmc.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _COLORLIST_H
#define _COLORLIST_H

typedef struct color_info 
{
   char*        name;
   unsigned int val;
} color_info;

extern const color_info color_list[];

extern int find_color(char *str, unsigned int *color);
extern int color_list_size(void);
extern int find_color_idx(const char *colorstr);

#endif /* _COLORLIST_H */
