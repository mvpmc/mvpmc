/*
 *  Copyright (C) 2004, 2005, Jon Gettler
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

#include <mvp_widget.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mwtypes.h>
#include <mvp_av.h>

#define WSS_PAL_ELEMENTS 137
#define WSS_NTSC_BITS 17
static mvp_widget_t *wss_surface = NULL;
static char wss_pal_elems[WSS_PAL_ELEMENTS];
static void wss_pal_update_surface();
static char wss_ntsc_bits[WSS_NTSC_BITS];
static void wss_ntsc_update_surface();
av_mode_t prev_mode = AV_MODE_UNKNOWN;
static int wss_init = 0;
static int wss_show_surface = 1;

#define WSS_PAL_Y_OFFSET 6 /*This seems to be line 23*/
#define WSS_NTSC_Y_OFFSET 3 /*This is pure guess and probably wrong, we're after lines 20 and 283 */
/*11 microseconds from the start of the sync pulse, should be 17 if we were
 *working in REC-601 pixels, but since we're not, subtract 9 to compensate*/
#define WSS_PAL_X_OFFSET 8 /*11 microseconds from the start of the sync pulse*/
#define WSS_NTSC_X_OFFSET 19 /*11.2 microseconds from the start of the sync pulse*/

/*This next number should actually be 2.7*720/702, but to avoid messing with
 * floating point stuff, we're dealing with offsets which are multiplied
 * by 100
 */
/* Annoyingly it would appear that our 720 pixels actually only represent
 * 702 REC 601 samples, so instead of this being a simple number (27 for 
 * 5Mhz WSS in 13.5Mhz)  */ 
#define PIXELS_PER_100_WSS_PAL_ELEMENTS  ((270 *720)/702)
#define PIXELS_PER_100_WSS_NTSC_BITS 3020 /*447.443kHz in 13.5MHz */
/*Add 1 on the end to force the number to be rounded up, and another 4 to make
 * sure we've got a bit of trailing black after the last high value
 */
#define WSS_NTSC_TOTAL_WIDTH ((int)(((PIXELS_PER_100_WSS_NTSC_BITS*WSS_NTSC_BITS)/100) +5))
#define WSS_PAL_TOTAL_PIXELS ((int)(((PIXELS_PER_100_WSS_PAL_ELEMENTS*WSS_PAL_ELEMENTS)/100) +5))

/*TODO: Check voltage levels on a proper scope, should be 500mV above black
 *on PAL, and 490mV above blanking on NTSC. These may be total rubbish
 *because there's probably some gamut correction after this.
 */
#define WSS_NTSC_PEAK 0xB2
#define WSS_PAL_PEAK 0xB6

void av_wss_visible(int newVal)
{
    if(newVal == wss_show_surface)
	return;
    wss_show_surface = newVal;
    if(!wss_show_surface)
    {
	if(wss_surface != NULL)
	{
	    mvpw_hide(wss_surface);
	    mvpw_destroy(wss_surface);
	    wss_surface = NULL;
	}
    }
}


static
void wss_update_surface()
{
    if(!wss_init)
	return;
    if(!wss_show_surface)
	return;
    av_mode_t cur_mode = av_get_mode();
    if(cur_mode != prev_mode || wss_surface == NULL)
    {
	if(wss_surface != NULL)
	{
	    mvpw_hide(wss_surface);
	    mvpw_destroy(wss_surface);
	    wss_surface = NULL;
	}
	if(cur_mode == AV_MODE_NTSC)
	{
	    wss_surface = mvpw_create_surface(NULL, WSS_NTSC_X_OFFSET,
#ifdef MVPMC_HOST
		    0,
#else
		    WSS_NTSC_Y_OFFSET,
#endif
		    WSS_NTSC_TOTAL_WIDTH, 2, MVPW_TRANSPARENT, 0, 0, 0);
	}
	else if(cur_mode == AV_MODE_PAL)
	{
	    wss_surface = mvpw_create_surface(NULL, WSS_PAL_X_OFFSET,
#ifdef MVPMC_HOST
		    0,
#else
		    WSS_PAL_Y_OFFSET,
#endif
		    WSS_PAL_TOTAL_PIXELS, 1, MVPW_TRANSPARENT, 0, 0, 0);
	}
	else
	    return;
    }
    prev_mode = cur_mode;
    if(cur_mode == AV_MODE_NTSC)
    {
	wss_ntsc_update_surface();
    }
    else if(cur_mode == AV_MODE_PAL)
    {
	wss_pal_update_surface();
    }
}

void av_wss_redraw()
{
    wss_update_surface();
}


void av_wss_update_aspect(av_wss_aspect_t new_wss_ar)
{
    if(wss_surface == NULL)
	return;
    printf("Setting WSS aspect to: %d\n",new_wss_ar);
    fflush(stdout);
    /* Do the pal bit first: */
    new_wss_ar &= 0xF;/*Don't want any spurious bits*/
    int i;
    for(i = 0;i<24;i++)
    {
	char val = new_wss_ar & 1;
	wss_pal_elems[i+53] = (i%6 < 3)? val: !val;
	if(i%6 == 5)
	    new_wss_ar >>=1;
    }

    /* Now the NTSC: */
    /* TODO: Check against an actual TV or IEC 61880 */
    int ntsc_mode;
    switch(new_wss_ar)
    {
	case WSS_ASPECT_FULL_16x9:
	    ntsc_mode = 1;
	    break;
	case WSS_ASPECT_BOX_16x9_CENTRE:
	case WSS_ASPECT_BOX_GT_16x9_CENTRE:
	    ntsc_mode = 2;
	    break;
	case WSS_ASPECT_UNKNOWN:
	case WSS_ASPECT_FULL_4x3:
	case WSS_ASPECT_FULL_4x3_PROTECT_14x9:
	default:
	    ntsc_mode = 0;
	    break;
    }
    wss_ntsc_bits[2] = ntsc_mode &1;
    wss_ntsc_bits[3] = (ntsc_mode &2)? 1:0;
    wss_update_surface();
}

void av_wss_init(void)
{
    /*Start by initialising PAL stuff*/
    /*put in runin/startcode as defined in ETSI EN 300 294*/
    long code = 0x1F1C71C7;
    long mask = 1<<28;
    int i;
    for(i = 0; i < 29; i++)
    {
	wss_pal_elems[i] = (code & mask)?1:0;
	code = code<<1;
    }
    code = 0x1E3C1F;
    mask = 1<<23;
    for(i = 29; i < 53; i++)
    {
	wss_pal_elems[i] = (code & mask)?1:0;
	code = code<<1;
    }
    /*Set the remainder of the WSS to bi-phase coded 0*/
    for(i = 0; i < WSS_PAL_ELEMENTS-53;i++)
    {
	wss_pal_elems[i+53] = (i%6 < 3)? 0:1;
    }

    /* And then trundle along to NTSC: */
    wss_ntsc_bits[0] = 1;
    wss_ntsc_bits[1] = 0;
    for(i =2;i < WSS_NTSC_BITS;i++)
    {
	wss_ntsc_bits[i] = 0;
    }

    wss_init = 1;
    wss_update_surface();
}

static void write_data(MWPIXELVAL * buf, const int buflen, const char * elems, const int nElems, const int pixels_per_100_elems, const char highVal)
{
    const MWPIXELVAL fullHighVal = (highVal<<16) | (highVal<<8) | highVal;
    int offset = 0;
    MWPIXELVAL c;
    int i;
    memset(buf,0,sizeof(*buf)*buflen);
    for(i = 0; i < nElems; i++)
    {
	if(elems[i])
	{
	    /* Work out which pixels this element falls over, and by how much */
	    int current,max;
	    max = offset + pixels_per_100_elems;
	    /* If we overlap partly onto a pixel, make that proportionally lit
	     * up
	     */
	    if(max %100)
	    {
		c  = highVal *(max %100)/100;
		c = c | c << 8 | c << 16;
		buf[max/100] = c;
	    }
	    max /= 100;
	    current =offset/100;
	    if(offset % 100)
	    {
		c  = highVal *(100 - (offset %100))/100;
		c = c | c << 8 | c << 16;
		buf[current] += c;
		current++;
	    }
	    c=fullHighVal;
	    for(;current < max;current++)
		buf[current] = c;
	}
	offset += pixels_per_100_elems;
    }

}

static void
wss_ntsc_update_surface()
{
    /* Start by calculating CRC:*/
    int i;
    char reg = 0;
    const char poly = 0x5;
    static MWPIXELVAL buf[WSS_NTSC_TOTAL_WIDTH];
    memset(&(wss_ntsc_bits[16]),1,6);
    for(i = 0;i<WSS_NTSC_BITS;i++)
    {
	/* Make sure we only have 1/0 values in here*/
	wss_ntsc_bits[i] = (wss_ntsc_bits[i])?1:0;
    }
    for(i = 2;i<WSS_NTSC_BITS;i++)
    {
	int perform_xor = (reg & 0x20)? 1:0;
	reg = ((reg << 1) | wss_ntsc_bits[i])&0x3F;
	if(perform_xor)
	{
	    reg = reg ^ poly;
	}
    }
    for(i = 0; i < 6;i++)
    {
	wss_ntsc_bits[16+i] = (reg&(1<<i))?1:0;
    }
    write_data(buf, WSS_NTSC_TOTAL_WIDTH, wss_ntsc_bits,
	            WSS_NTSC_BITS, PIXELS_PER_100_WSS_NTSC_BITS,
		    WSS_NTSC_PEAK);

    mvpw_show(wss_surface);
    /*NTSC WSS appears on both fields, so make it 2 lines long:*/
    mvpw_set_surface(wss_surface,(char *)buf,0,0,WSS_NTSC_TOTAL_WIDTH,1);
    mvpw_set_surface(wss_surface,(char *)buf,0,1,WSS_NTSC_TOTAL_WIDTH,1);
}

static void
wss_pal_update_surface()
{
    static MWPIXELVAL buf[WSS_PAL_TOTAL_PIXELS];
    /*The "elements" in wss_elements are written out at 5Mhz as described
     * in ETSI EN 300 294 */

    write_data(buf, WSS_PAL_TOTAL_PIXELS, wss_pal_elems,
	            WSS_PAL_ELEMENTS, PIXELS_PER_100_WSS_PAL_ELEMENTS,
		    WSS_PAL_PEAK);
	/* IMPORTANT! Widget *must* be shown before trying to write to it
	 * otherwise the write will fail silently on the MVP
	 */
    mvpw_show(wss_surface);
    mvpw_set_surface(wss_surface,(char *)buf,0,0,WSS_PAL_TOTAL_PIXELS,1);
    
}

