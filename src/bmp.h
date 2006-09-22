/*
 *  Copyright (C) 2005-2006, Jon Gettler
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

#ifndef BMP_H
#define BMP_H

typedef struct {
	unsigned char magic[2];
	unsigned long size;
	unsigned short reserved1;
	unsigned short reserved2;
	unsigned long offset;
} __attribute__ ((packed)) bmp_file_header_t;

typedef struct {
	unsigned long size;
	unsigned long width;
	unsigned long height;
	unsigned short planes;
	unsigned short bitcount;
	unsigned long compression;
	unsigned long size_image;
	unsigned long xppm;
	unsigned long yppm;
	unsigned long used;
	unsigned long important;
} __attribute__ ((packed)) bmp_image_header_t;

typedef struct {
	bmp_file_header_t fheader;
	bmp_image_header_t iheader;
	unsigned char image[0];
} __attribute__ ((packed)) bmp_file_t;

extern bmp_file_t* bmp_read(int fd);

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define le_long(x) x
#define le_short(x) x
#elif __BYTE_ORDER == __BIG_ENDIAN
unsigned long
le_long(unsigned long l)
{
	unsigned char *ptr = (unsigned char*)&l;
        unsigned long out;

        out = (ptr[0] << 0) | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);

        return out;
}

unsigned short
le_short(unsigned short s)
{
	unsigned char *ptr = (unsigned char*)&s;
	unsigned short out;

	out = (ptr[0] << 0) | (ptr[1] << 8);

	return out;
}
#else
#error uknown endian
#endif

#endif /* BMP_H */
