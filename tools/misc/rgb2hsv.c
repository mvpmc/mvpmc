/*
 *  Copyright (C) 2004, John Honeycutt
 *  http://mvpmc.sourceforge.net/
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STRTOHEX(x) strtoul((x), NULL, 16)
#define STRTODEC(x) strtol((x), NULL, 10)


// r,g,b values are from 0 to 1
// h = [0,360], s = [0,1], v = [0,1]
//		if s == 0, then h = -1 (undefined)

float MIN( float r, float g, float b )
{
   float tmp;
   if ( r < g ) {
      tmp = r;
   }
   else {
      tmp = g;
   }
   if ( tmp < b ) {
      return(tmp);
   }
   else {
      return(b);
   }
}

float MAX( float r, float g, float b )
{
   float tmp;
   if ( r > g ) {
      tmp = r;
   }
   else {
      tmp = g;
   }
   if ( tmp > b ) {
      return(tmp);
   }
   else {
      return(b);
   }
}

void RGBtoHSV( float r, float g, float b, float *h, float *s, float *v )
{
   float min, max, delta;
   
   min = MIN( r, g, b );
   max = MAX( r, g, b );
   *v = max;				// v
   
   delta = max - min;
   
   if( max != 0 ) {
      //printf("%3.3f %3.3f\n", delta, max);
      *s = delta / max;
   }
   else {
      // r = g = b = 0		// s = 0, v is undefined
      *s = 0;
      *h = -1;
      return;
   }
   
   if( r == max )
      *h = ( g - b ) / delta;		// between yellow & magenta
   else if( g == max )
      *h = 2 + ( b - r ) / delta;	// between cyan & yellow
   else
      *h = 4 + ( r - g ) / delta;	// between magenta & cyan
   
   *h *= 60;				// degrees
   if( *h < 0 )
      *h += 360;
   
}

void HSVtoRGB( float *r, float *g, float *b, float h, float s, float v )
{
   int i;
   float f, p, q, t;
   
   if( s == 0 ) {
      // achromatic (grey)
      *r = *g = *b = v;
      return;
   }
   
   h /= 60;			// sector 0 to 5
   i = floor( h );
   f = h - i;			// factorial part of h
   p = v * ( 1 - s );
   q = v * ( 1 - s * f );
   t = v * ( 1 - s * ( 1 - f ) );
   
   switch( i ) {
   case 0:
      *r = v;
      *g = t;
      *b = p;
      break;
   case 1:
      *r = q;
      *g = v;
      *b = p;
      break;
   case 2:
      *r = p;
      *g = v;
      *b = t;
      break;
   case 3:
      *r = p;
      *g = q;
      *b = v;
      break;
   case 4:
      *r = t;
      *g = p;
      *b = v;
      break;
   default:		// case 5:
      *r = v;
      *g = p;
      *b = q;
      break;
   }  
}

int main( int argc, char *argv[] )
{
   
   float r, g, b, h, s, v;
   
   if ( argc < 4 ) {
      printf("need rgb vals\n");
      exit(0);
   }

   r = (float)STRTODEC(argv[1]) / (float)255;
   g = (float)STRTODEC(argv[2]) / (float)255;
   b = (float)STRTODEC(argv[3]) / (float)255;

   //printf("r=%3.3f g=%3.3f b=%3.3f\n", r, g, b);
   RGBtoHSV(r, g, b, &h, &s, &v);
   printf("h=%3.6f s=%3.6f v=%3.6f", h, s, v);
   //printf("\n");
   exit(0);
   
}
