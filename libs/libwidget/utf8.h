/*
 *  $Id$
 *
 *  Copyright (C) 2004, Erik Corry
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

#ifndef LIBWIDGET_UTF8_H
#define LIBWIDGET_UTF8_H

extern int utf8_char_count(char *string);
typedef void per_character_helper_t(void *closure, int c);
typedef void per_character_helper2_t(void *closure, void *closure2, int c);
extern void utf8_for_each(char *string, per_character_helper_t *fn, void *closure);
extern void utf8_for_each2(char *string, per_character_helper2_t *fn, void *closure, void *closure2);

#endif /* LIBWIDGET_UTF8_H */
