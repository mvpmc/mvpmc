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

#ifndef HEADEND_H
#define HEADEND_H

#include "rtv.h"

extern struct mapping device_mapping[];
extern struct mapping device_bitmapping[];
extern struct mapping service_tier_mapping[];

struct headend_header {
/*  0 */ __u32 device_bitmap;
/*  4 */ __u8  max_tier[32];
}; /* 36 */

struct headend_channel {
/*  0 */ __u32 tmsid;
/*  4 */ __u16 tuning;
/*  6 */ __u8  device;
/*  7 */ __u8  service_tier;
/*  8 */ __u8  name[16];
/* 24 */ __u8  description[32];
}; /* 56 */

int parse_headend_header(unsigned char **p, struct headend_header *h);
int parse_headend_channel(unsigned char **p, struct headend_channel *hc);

void dump_headend_header(struct headend_header *h);
void dump_headend_channel(struct headend_channel *hc);

#endif
