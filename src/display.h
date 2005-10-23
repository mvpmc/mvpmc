#ifndef DISPLAY_H
#define DISPLAY_H

/*
 *  $Id$
 *
 *  Copyright (C) 2005, Rick Stuart
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


extern void display_send(char *);
extern void display_iee_40x2(void);
extern void display_iee_16x1(void);

#define DISPLAY_MESG_SIZE	200
#define DISPLAY_SUBMESG_SIZE	50

extern char display_message[DISPLAY_MESG_SIZE];

/*
 * Track which local display type is selected on the command line.
 */
extern int display_type;

#define DISPLAY_DISABLE		0
#define DISPLAY_IEE16X1		1
#define DISPLAY_IEE40X2		2

#define DISPLAY_IEE16X1_WIDTH	16
#define DISPLAY_IEE16X1_HEIGTH	1
#define DISPLAY_IEE40X2_WIDTH	40
#define DISPLAY_IEE40X2_HEIGTH	2

#define DISPLAY_IEE_BACKSP	0x08
#define DISPLAY_IEE_FWD		0x09
#define DISPLAY_IEE_LF		0x0a
#define DISPLAY_IEE_CR		0x0d
#define DISPLAY_IEE_CURINV	0x0e
#define DISPLAY_IEE_CURVIS	0x0f
#define DISPLAY_IEE_CRAUTO	0x11
#define DISPLAY_IEE_CROFF	0x12
#define DISPLAY_IEE_HZSCORLL	0x13
#define DISPLAY_IEE_RESET	0x14
#define DISPLAY_IEE_CLR		0x15
#define DISPLAY_IEE_HOME	0x16
#define DISPLAY_IEE_FONTLD	0x18
#define DISPLAY_IEE_BIT7	0x19
#define DISPLAY_IEE_POSITION	0x1b
/*
 * Next byte is position, examples:
 * 0000 0000=1st pos line 1
 * 0100 0000=1st pos line 2
 * 0100 0001=2nd pos line 2
 * 0101 0100=20th pos line 2
 *
 * Because embedding the first position into a char str
 * terms it (zero looks like EndOfStr), add 1 to all
 * position vals and sub when sending to disp.
 */
#define DISPLAY_IEE_POS_0_0	(0x00 + 1)
#define DISPLAY_IEE_POS_0_1	(0x01 + 1)
#define DISPLAY_IEE_POS_1_0	(0x40 + 1)
#define DISPLAY_IEE_POS_1_1	(0x41 + 1)
#define DISPLAY_IEE_POS_1_20	(0x54 + 1)
////#define DISPLAY_IEE_POS_1_20	(0x53 + 1)

#define DISPLAY_IEE_DIM		0x1d
#define DISPLAY_IEE_BRIGHT	0x1e
#define DISPLAY_IEE_BRIGHTEST	0x1f


#define DIMMER_EVENT_WAIT	40

/*
 * Using nanoseconds for waiting:
 *
 *              struct timespec
 *              {
 *                      time_t  tv_sec;    //seconds
 *                      long    tv_nsec;   //nanoseconds
 *              };
 *       The  value  of  the nanoseconds field must be in the range 0 to 999 999
 *       999.
 */
#define DISPLAY_LONG_INTERVAL	999999999
#define DISPLAY_NORMAL_INTERVAL	250000000
#define DISPLAY_SCROLL_INTERVAL	50000000

#endif /* DISPLAY_H */
