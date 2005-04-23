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

#include <ctype.h>
#include <stdio.h>
#include <time.h>

#include "rtv.h"
#include "dump.h"

static FILE *fp;
static char spaces[] = "                                ";
static char *leader = spaces + sizeof(spaces) - 1;

void dump_set_file(FILE *out)
{
    fp = out;
}

void dump_group_start(char const *groupname)
{
    fprintf(fp, "%s%s {\n", leader, groupname);
    leader -= 2;
}

void dump_group_end(void)
{
    leader += 2;
    fprintf(fp, "%s}\n", leader);
}

void dump_mapping(char const *tag, __u32 value, struct mapping *map)
{
    int i;

    for (i = 0; map[i].name; i++) {
        if (map[i].value == value) {
            fprintf(fp, "%s%-24s\t%lu %s\n",
                    leader, tag, (unsigned long)value, map[i].name);
            return;
        }
    }
    fprintf(fp, "%s%-24s\t%lu Unrecognized item\n",
            leader, tag, (unsigned long)value);
}

void dump_bitmapping(char const *tag, __u32 value, struct mapping *map)
{
    int i;
    int first = 1;
    
    fprintf(fp, "%s%-24s\t%-8lx     ", leader, tag, (unsigned long)value);

    for (i = 0; map[i].name; i++) {
        if (value & map[i].value) {
            fprintf(fp, "%s%s", first ? "" : "; ", map[i].name);
            first = 0;
            value &= ~map[i].value;
            if (value == 0) {
                break;
            }
        }
    }
    if (value == 0) {
        fprintf(fp, "\n");
        return;
    }
    fprintf(fp, "& %02lx\n", (unsigned long) value);
}

void dump_time(char const *tag, __u32 value)
{
    char buffer[32];
    struct tm * tm;

    tm = localtime(&value);
    strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M:%S", tm);
    fprintf(fp, "%s%-24s\t%10lu             %s\n",
            leader, tag, (unsigned long)value, buffer);
}

void dump_string(char const *tag, char const *value)
{
    fprintf(fp, "%s%-24s\t%s\n", leader, tag, value);
}

void dump_u8(char const *tag, __u8 value)
{
    fprintf(fp, "%s%-24s\t      0x%02x %12u\n", leader, tag, value, value);
}

void dump_u16(char const *tag, __u16 value)
{
    fprintf(fp, "%s%-24s\t    0x%04x %12u\n", leader, tag, value, value);
}

void dump_u32(char const *tag, __u32 value)
{
    if (value > 0x80000000)
        fprintf(fp, "%s%-24s\t0x%08lx %12lu  %12lu\n", leader, tag,
               (unsigned long)value, (unsigned long)value,
               (unsigned long)value - 0x80000000);
    else
        fprintf(fp, "%s%-24s\t0x%08lx %12lu\n", leader, tag,
               (unsigned long)value, (unsigned long)value);
}

void dump_u64(char const *tag, __u64 value)
{
    fprintf(fp, "%s%-24s\t0x%016"U64F"x %24"U64F"u\n",
            leader, tag, value, value);
}

void dump_buffer(char const *tag, __u8 const *buffer, size_t len) 
{
    unsigned int row, column, byte;
#define COLUMNS 16
    
    fprintf(fp, "%s%-24s\t", leader, tag);
    for (row = 0; row < (len + COLUMNS - 1)/COLUMNS; row++) {
        if (row > 0)
            fprintf(fp, "%s                        \t", leader);
        for (column = 0,       byte = row * COLUMNS;
             column < COLUMNS;
             column++,         byte++) {
            if (byte < len) {
                fprintf(fp, "%02x", buffer[byte]);
            } else {
                fprintf(fp, "  ");
            }
            if (byte % 4 == 3)
                fprintf(fp, " ");
        }
        for (column = 0,       byte = row * COLUMNS;
             column < COLUMNS;
             column++,         byte++) {
            if (byte < len) {
                fprintf(fp, "%c",
                        isascii(buffer[byte]) && isprint(buffer[byte]) ?
                        buffer[byte] : '.');
            } else {
                fprintf(fp, " ");
            }
        }
        fprintf(fp, "\n");
    }
#undef COLUMNS
}

char const *lookup_mapping(__u32 value, struct mapping *map)
{
    int i;

    for (i = 0; map[i].name; i++)
        if (map[i].value == value)
            return map[i].name;
    return NULL;
}
