/*
 * Copyright (C) 2004 John Honeycutt
 * Copyright (C) 2002 John Todd Larason <jtl@molehill.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */

#ifndef BIGFILE_H
#define BIGFILE_H

#include "rtv.h"
#include <stdio.h>

struct big_file;
typedef struct big_file BIGFILE;
BIGFILE  *bfopen(const char *filename, const char *mode);
BIGFILE  *bfreopen(FILE *fp);
int       bfseek(BIGFILE *stream, s64 offset, int whence);
u64       bftell(BIGFILE *stream);
size_t    bfread(void *ptr, size_t size, size_t nmemb, BIGFILE *stream);
size_t    bfwrite(const void *ptr, size_t size, size_t nmemb, BIGFILE *stream);
int       bfclose(BIGFILE *stream);
#endif
