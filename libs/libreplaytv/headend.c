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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtv.h"
#include "headend.h"
#include "dump.h"

struct mapping device_mapping[] = {  /* combines both numeric and text keys */
    { 0, "Standard" },
    { 1, "A Lineup" },
    { 2, "B Lineup" },
    { 3, "C" },
    { 4, "Rebuild Lineup" },
    { 5, "E" },
    { 6, "F" },
    { 7, "Non-Addressable Converter" },
    { 8, "Hamlin" },
    { 9, "Jerrold Impulse" },
    { 10, "Jerrold" },
    { 11, "K" },
    { 12, "Digital Rebuild" },
    { 13, "Multiple Converters" },
    { 14, "Pioneer" },
    { 15, "Oak" },
    { 16, "Premium" },
    { 17, "Q" },
    { 18, "Cable-ready-TV" },
    { 19, "Converter Switch" },
    { 20, "Tocom" },
    { 21, "A Lineup Cable-ready-TV" },
    { 22, "B Lineup Cable-ready-TV" },
    { 23, "Scientific-Atlanta" },
    { 24, "Digital" },
    { 25, "Y" },
    { 26, "Zenith" },
    { '@', "Standard" },
    { 'A', "A Lineup" },
    { 'B', "B Lineup" },
    { 'C', "C" },
    { 'D', "Rebuild Lineup" },
    { 'E', "E" },
    { 'F', "F" },
    { 'G', "Non-Addressable Converter" },
    { 'H', "Hamlin" },
    { 'I', "Jerrold Impulse" },
    { 'J', "Jerrold" },
    { 'K', "K" },
    { 'L', "Digital Rebuild" },
    { 'M', "Multiple Converters" },
    { 'N', "Pioneer" },
    { 'O', "Oak" },
    { 'P', "Premium" },
    { 'Q', "Q" },
    { 'R', "Cable-ready-TV" },
    { 'S', "Converter Switch" },
    { 'T', "Tocom" },
    { 'U', "A Lineup Cable-ready-TV" },
    { 'V', "B Lineup Cable-ready-TV" },
    { 'W', "Scientific-Atlanta" },
    { 'X', "Digital" },
    { 'Y', "Y" },
    { 'Z', "Zenith" },
    { -1, NULL}
};

struct mapping device_bitmapping[] = {  /* just like the above, but the
                                           shifted-bit version */
    { 1 << 0, "Standard" },
    { 1 << 1, "A Lineup" },
    { 1 << 2, "B Lineup" },
    { 1 << 3, "C" },
    { 1 << 4, "Rebuild Lineup" },
    { 1 << 5, "E" },
    { 1 << 6, "F" },
    { 1 << 7, "Non-Addressable Converter" },
    { 1 << 8, "Hamlin" },
    { 1 << 9, "Jerrold Impulse" },
    { 1 << 10, "Jerrold" },
    { 1 << 11, "K" },
    { 1 << 12, "Digital Rebuild" },
    { 1 << 13, "Multiple Converters" },
    { 1 << 14, "Pioneer" },
    { 1 << 15, "Oak" },
    { 1 << 16, "Premium" },
    { 1 << 17, "Q" },
    { 1 << 18, "Cable-ready-TV" },
    { 1 << 19, "Converter Switch" },
    { 1 << 20, "Tocom" },
    { 1 << 21, "A Lineup Cable-ready-TV" },
    { 1 << 22, "B Lineup Cable-ready-TV" },
    { 1 << 23, "Scientific-Atlanta" },
    { 1 << 24, "Digital" },
    { 1 << 25, "Y" },
    { 1 << 26, "Zenith" },
    { -1, NULL}
};

struct mapping service_tier_mapping[] = {
    { 1, "Basic" },
    { 2, "Extended Basic" },
    { 3, "Premium" },
    { 4, "Pay-Per-View" },
    { 5, "Music" },
    {-1, NULL }
};

int parse_headend_header(unsigned char ** pp, struct headend_header * h)
{
    unsigned char * p = *pp;

    h->device_bitmap = ntohl(*p);
    memcpy(h->max_tier, p, 32); p += 32;

    *pp = p;

    return 0;
}

int parse_headend_channel(unsigned char ** pp, struct headend_channel * hc)
{
    unsigned char * p = *pp;

    hc->tmsid        = ntohl(*p);
    hc->tuning       = ntohs(*p);
    hc->device       = *p++;
    hc->service_tier = *p++;
    memcpy(hc->name, p, 16); p += 16;
    memcpy(hc->description, p, 32); p += 32;

    *pp = p;

    return 0;
}

void dump_headend_header(struct headend_header * h)
{
    int i;
    
    dump_group_start    ("Headend Header");
    dump_bitmapping     ("Expected Devices",    h->device_bitmap, device_bitmapping);
    if (h->device_bitmap) {
        dump_group_start("Maximum Device Service Tier");
        for (i = 0; i < 32; i++)
            if (h->device_bitmap & (1 << i))
                dump_mapping(lookup_mapping(i, device_mapping),
                             h->max_tier[i], service_tier_mapping);
        dump_group_end  ();
    }
    dump_group_end      ();
}

void dump_headend_channel(struct headend_channel * hc) 
{
    dump_group_start    ("Headend Channel");
    dump_u32            ("TMS ID",              hc->tmsid);
    dump_u16            ("Tuning",              hc->tuning);
    dump_mapping        ("Device",              hc->device, device_mapping);
    dump_mapping        ("Service Tier",        hc->service_tier, service_tier_mapping);
    dump_string         ("Name",                hc->name);
    dump_string         ("Description",         hc->description);
    dump_group_end      ();
#if 0
    RTV_PRT("%-16s %3d %-8s %5d %-8.8s %s\n",
            device_name(device),
            channel,
            tier_name(tier),
            tmsid,
            name,
            description
       );
#endif
}

