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

#if defined(__unix__) && !defined(__FreeBSD__)
#   include <netinet/in.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "rtv.h"

u32 rtv_debug = 0x00000000;
//u32 rtv_debug = 0x100000ff;

void hex_dump(char * tag, unsigned char * buf, size_t sz)
{
    unsigned int rows, row, col, i, c;
    unsigned long addr;
    RTV_PRT("rtv:HEX DUMP: %s\n", tag);
    rows = (sz + 15)/16;
    for (row = 0; row < rows; row++) {
        addr = (unsigned long)(buf + (row*16));
        RTV_PRT("0x%08lx | ", addr);
        for (col = 0; col < 16; col++) {
            i = row * 16 + col;
            if (i < sz)
                RTV_PRT("%02x", buf[i]);
            else
                RTV_PRT("  ");
            if ((i & 3) == 3)
                RTV_PRT(" ");
        }
        RTV_PRT("  |  ");
        for (col = 0; col < 16; col++) {
            i = row * 16 + col;
            if (i < sz) {
                c = buf[i];
                RTV_PRT("%c", (c >= ' ' && c <= '~') ? c : '.');
            } else {
                RTV_PRT(" ");
            }
            if ((i & 3) == 3)
                RTV_PRT(" ");
        }
        RTV_PRT(" |\n");
    }
}

int split_lines(char * src, char *** pdst)
{
    int num_lines, i;
    char * p;
    char ** dst;

    num_lines = 0;
    p = src;
    while (p) {
        p = strchr(p, '\n');
        if (p) {
            p++;
            num_lines++;
        }
    }

    dst = malloc((num_lines + 1) * sizeof(char *));
    dst[0] = src;
    p = src;
    i = 1;
    while (p) {
        p = strchr(p, '\n');
        if (p) {
            *p = '\0';
            p++;
            dst[i] = p;
            i++;
        }
    }

    *pdst = dst;

    return num_lines;
}

