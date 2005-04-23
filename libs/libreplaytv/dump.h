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

#ifndef DUMP_H
#define DUMP_H
#include "rtv.h"
#include <stdlib.h>
#include <stdio.h>

void dump_set_file(FILE *out);
void dump_group_start(char const *groupname);
void dump_group_end(void);
void dump_mapping(char const *tag, __u32 value, struct mapping *map);
void dump_bitmapping(char const *tag, __u32 value, struct mapping *map);
void dump_time(char const *tag, __u32 value);
void dump_string(char const *tag, char const *value);
void dump_u8(char const *tag, __u8 value);
void dump_u16(char const *tag, __u16 value);
void dump_u32(char const *tag, __u32 value);
void dump_u64(char const *tag, __u64 value);
void dump_buffer(char const *tag, __u8 const *buffer, size_t len);

char const *lookup_mapping(__u32 value, struct mapping *map);

#endif
