/*
 *  Copyright (C) 2004-2006, Jon Gettler
 *  http://www.mvpmc.org/
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "mvp_osd.h"

#include "surface.h"
#include "osd.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

int stbgfx = -1;

/*
 * RGB to YUV conversion tables
 */
static int conv_YB[256];
static int conv_YG[256];
static int conv_YR[256];
static int conv_UB[256];
static int conv_UG[256];
static int conv_UR[256];
static int conv_VB[256];
static int conv_VG[256];
static int conv_VR[256];

static int conv_BY[256];
static int conv_GY[256];
static int conv_RY[256];
static int conv_BU[256];
static int conv_GU[256];
static int conv_RU[256];
static int conv_BV[256];
static int conv_GV[256];
static int conv_RV[256];

/*
 * gfx_init() - initialize the RGB to YUV conversion tables
 */
void
gfx_init(void)
{
	int i;

	PRINTF("gfx_init(): initialize\n");

	for (i=0; i<256; i++) {
		conv_YB[i] = 0.299 * (double)i;
		conv_BY[i] = i;
	}
	for (i=0; i<256; i++) {
		conv_YG[i] = 0.587 * (double)i;
		conv_GY[i] = i;
	}
	for (i=0; i<256; i++) {
		conv_YR[i] = 0.114 * (double)i;
		conv_RY[i] = i;
	}

	for (i=0; i<256; i++) {
		conv_UB[i] = 0.5 * (double)i;
		conv_BU[i] = 1.732 * (i - 128);
	}
	for (i=0; i<256; i++) {
		conv_UG[i] = -0.33126 * (double)i;
		conv_GU[i] = -0.338 * (i - 128);
	}
	for (i=0; i<256; i++) {
		conv_UR[i] = -0.16874 * (double)i;
		conv_RU[i] = 0;
	}

	for (i=0; i<256; i++) {
		conv_VB[i] = -0.08131 * (double)i;
		conv_BV[i] = 0;
	}
	for (i=0; i<256; i++) {
		conv_VG[i] = -0.41869 * (double)i;
		conv_GV[i] = -0.698 * (i - 128);
	}
	for (i=0; i<256; i++) {
		conv_VR[i] = 0.5 * (double)i;
		conv_RV[i] = 1.370 * ((double)i - 128);
	}
}

/*
 * rgb2yuv() - convert an RGB pixel to YUV
 */
void
rgb2yuv(unsigned char r, unsigned char g, unsigned char b,
	unsigned char *y, unsigned char *u, unsigned char *v)
{
	int Y, U, V;

	Y  = (unsigned char)((8432*(unsigned long)r +
			      16425*(unsigned long)g +
			      3176*(unsigned long)b +
			      16*32768)>>15);
	U = (unsigned char)((128*32768 +
			     14345*(unsigned long)b -
			     4818*(unsigned long)r -
			     9527*(unsigned long)g)>>15);
	V = (unsigned char)((128*32768 +
			     14345*(unsigned long)r -
			     12045*(unsigned long)g -
			     2300*(unsigned long)b)>>15);

	*y = Y;
	*u = U;
	*v = V;
}

/*
 * yuv2rgb() - convert a YUV pixel to RGB
 */
void
yuv2rgb(unsigned char y, unsigned char u, unsigned char v,
	unsigned char *r, unsigned char *g, unsigned char *b)
{
	int R, G, B, Y;

	Y = 38142*(int)y;

	R = (Y + 52298*(int)v - 7287603)>>15;

	if (R > 255)
		R = 255;
	else if (R < 0)
		R = 0;

	G = (Y + 4439671 - 26640*v - 12812*u)>> 15;

	if (G > 255)
		G = 255;
	else if (G < 0)
		G = 0;
    
	B = (Y + 66126*u - 9074377)>>15;

	if (B > 255)
		B = 255;
	else if (B < 0)
		B = 0;

	*r = R;
	*g = G;
	*b = B;
}

int
osd_open(void)
{
	return 0;
}

int
osd_close(void)
{
	close(stbgfx);
	stbgfx = -1;

	return 0;
}
