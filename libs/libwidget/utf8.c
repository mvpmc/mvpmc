/*
 *  Copyright (C) 2006, Erik Corry
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

#ident "$Id$"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "utf8.h"

void for_each_helper(void *closure, void *closure2, int c) {
	per_character_helper_t *fn = (per_character_helper_t *)closure2;
	fn(closure, c);
}

void utf8_for_each(char *string, per_character_helper_t *fn, void *closure) {
	utf8_for_each2(string, &for_each_helper, closure, (void *)fn);
}

void utf8_for_each2(char *string, per_character_helper2_t *fn, void *closure, void *closure2) {
	int stringlen = strlen(string);
	int i;
	for (i = 0; i < stringlen; i++) {
		unsigned char *x = (unsigned char*)&(string[i]);
		if ((*x) >= 0xf0) {
			int c = ((*x) & 0x07) << 18;
			c |= ((*(x+1)) & 0x3f) << 12;
			c |= ((*(x+2)) & 0x3f) << 6;
			c |= ((*(x+3)) & 0x3f);
			i+=3;
			fn(closure, closure2, c);
		} else if ((*x) >= 0xe0) {
			int c = ((*x) & 0x0f) << 12;
			c |= ((*(x+1)) & 0x3f) << 6;
			c |= ((*(x+2)) & 0x3f);
			fn(closure, closure2, c);
			i+=2;
		} else if ((*x) >= 0xc0) {
			int c = ((*x) & 0x1f) << 6;
			c |= ((*(x+1)) & 0x3f);
			fn(closure, closure2, c);
			i++;
		} else {
			fn(closure, closure2, *x);
		}
	}
}

static void increment(void *closure, int c) {
	int *len = (int *)closure;
	(*len)++;
}

int utf8_char_count(char *string) {
	int l = 0;
	utf8_for_each(string, increment, (void *)&l);
	return l;
}
