/*
 * Copyright (c) 1999 Greg Haerr <greg@censoft.com>
 * Copyright (c) 1991 David I. Bell
 * Permission is granted to use, distribute, or modify this source,
 * provided that this copyright notice remains intact.
 *
 * Hauppauge MediaMVP /dev/rawir IR Remote Keyboard Driver by erl
 */
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <stdio.h>
#include "device.h"

/*
 * buttons for new and old remote
 */
#define	ZERO_BUTTON		 0
#define	ONE_BUTTON		 1
#define	TWO_BUTTON		 2
#define	THREE_BUTTON		 3
#define	FOUR_BUTTON		 4
#define	FIVE_BUTTON		 5
#define	SIX_BUTTON		 6
#define	SEVEN_BUTTON		 7
#define	EIGHT_BUTTON		 8
#define	NINE_BUTTON		 9
#define	RED_BUTTON		11
#define	BLANK_BUTTON		12
#define	MENU_BUTTON		13
#define	MUTE_BUTTON		15
#define	VOL_UP_BUTTON		16
#define	VOL_DOWN_BUTTON		17
#define	SKIP_BUTTON		30
#define	EXIT_BUTTON		31
#define	REPLAY_BUTTON		36
#define	OK_BUTTON		37
#define	BLUE_BUTTON		41
#define	GREEN_BUTTON		46
#define	PAUSE_BUTTON		48
#define	REW_BUTTON		50
#define	FF_BUTTON		52
#define	PLAY_BUTTON		53
#define	STOP_BUTTON		54
#define	REC_BUTTON		55
#define	YELLOW_BUTTON		56
#define	GO_BUTTON		59
#define	FULL_BUTTON		60
#define	POWER_BUTTON		61

/*
 * buttons which need to be remapped
 */
#define	UP_BUTTON		20
#define	DOWN_BUTTON		21
#define	CHAN_UP_BUTTON		32
#define	CHAN_DOWN_BUTTON	33
#define	RIGHT_BUTTON		23
#define	LEFT_BUTTON		22

/*
 * buttons only on new remote
 */
#define	VIDEOS_BUTTON		24
#define	MUSIC_BUTTON		25
#define	PICTURES_BUTTON		26
#define	GUIDE_BUTTON		27
#define	TV_BUTTON		    28
#define	RADIO_BUTTON		29
#define	ASTERISK_BUTTON		10
#define	POUND_BUTTON		14
#define	PREV_CHAN_BUTTON	18

#define	KEYBOARD	"/dev/rawir"	/* keyboard associated with screen*/

static int  IRM_Open(KBDDEVICE *pkd);
static void IRM_Close(void);
static void IRM_GetModifierInfo(MWKEYMOD *modifiers, MWKEYMOD *curmodifiers);
static int  IRM_Read(MWKEY *kbuf, MWKEYMOD *modifiers, MWSCANCODE *scancode);

KBDDEVICE kbddev = {
	IRM_Open,
	IRM_Close,
	IRM_GetModifierInfo,
	IRM_Read,
	NULL
};

static	int		fd;		/* file descriptor for keyboard */

/*
 * Open the keyboard.
 * This is real simple, we just use a special file handle
 * that allows non-blocking I/O, and put the terminal into
 * character mode.
 */
static int
IRM_Open(KBDDEVICE *pkd)
{
	fd = open(KEYBOARD, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return -1;

	return fd;

}

/*
 * Close the keyboard.
 * This resets the terminal modes.
 */
static void
IRM_Close(void)
{
	close(fd);
	fd = -1;
}

/*
 * Return the possible modifiers for the keyboard.
 */
static  void
IRM_GetModifierInfo(MWKEYMOD *modifiers, MWKEYMOD *curmodifiers)
{
	*modifiers = 0;			/* no modifiers available */
}

/*
 * This reads one keystroke from the keyboard, and the current state of
 * the mode keys (ALT, SHIFT, CTRL).  Returns -1 on error, 0 if no data
 * is ready, and 1 if data was read.  This is a non-blocking call.
 */
static int
IRM_Read(MWKEY *buf, MWKEYMOD *modifiers, MWSCANCODE *scancode)
{
	unsigned short	in;
	int cc;			/* characters read */
	MWKEY key;

	*modifiers = 0;			/* no modifiers yet */
	cc = read(fd, &in, sizeof(in));
	if ((cc < 0) && (errno != EINTR) && (errno != EAGAIN)) {
		perror("kbd");
		return -1;
	}
	if (cc == sizeof(in)) {
		/*
		 * Old remote has upper bytes 0x00
		 * New remote has upper bytes 0x01 (except power button)
		 */
		if ((in & 0xFE00) == 0) {
			key = in & 0xff;
			switch(key) {
				case POWER_BUTTON:
				case GO_BUTTON:
				case ZERO_BUTTON:
				case ONE_BUTTON:
				case TWO_BUTTON:
				case THREE_BUTTON:
				case FOUR_BUTTON:
				case FIVE_BUTTON:
				case SIX_BUTTON:
				case SEVEN_BUTTON:
				case EIGHT_BUTTON:
				case NINE_BUTTON:
				case EXIT_BUTTON:
				case MENU_BUTTON:
				case RED_BUTTON:
				case GREEN_BUTTON:
				case YELLOW_BUTTON:
				case BLUE_BUTTON:
				case MUTE_BUTTON:
				case FULL_BUTTON:
				case REW_BUTTON:
				case PLAY_BUTTON:
				case FF_BUTTON:
				case REC_BUTTON:
				case STOP_BUTTON:
				case PAUSE_BUTTON:
				case REPLAY_BUTTON:
				case SKIP_BUTTON:
				case OK_BUTTON:
				case ASTERISK_BUTTON:
				case POUND_BUTTON:
				case PREV_CHAN_BUTTON:
               case VIDEOS_BUTTON:
               case MUSIC_BUTTON:
               case PICTURES_BUTTON:
               case GUIDE_BUTTON:
               case TV_BUTTON:	
					*buf = key;
					break;
				/*
				 * Volume up/down are shared with right/left
				 * on the old remote.
				 */
				case RIGHT_BUTTON:
					if (in & 0xff00)
						*buf = VOL_UP_BUTTON;
					else
						*buf = key;
					break;
				case LEFT_BUTTON:
					if (in & 0xff00)
						*buf = VOL_DOWN_BUTTON;
					else
						*buf = key;
					break;
				case VOL_UP_BUTTON:
					if (in & 0xff00)
						*buf = RIGHT_BUTTON;
					else
						*buf = key;
					break;
				case VOL_DOWN_BUTTON:
					if (in & 0xff00)
						*buf = LEFT_BUTTON;
					else
						*buf = key;
					break;
				/*
				 * The blank button on the old remote and
				 * the radio button on the new are the same.
				 */
				case BLANK_BUTTON:
					if (in & 0xff00)
						*buf = RADIO_BUTTON;
					else
						*buf = key;
					break;
				/*
				 * Remap UP and DOWN so the new and old
				 * remotes match.
				 */
				case UP_BUTTON:
					if (in & 0xff00)
						*buf = CHAN_UP_BUTTON;
					else
						*buf = key;
					break;
				case DOWN_BUTTON:
					if (in & 0xff00)
						*buf = CHAN_DOWN_BUTTON;
					else
						*buf = key;
					break;
				case CHAN_UP_BUTTON:
					if (in & 0xff00)
						*buf = UP_BUTTON;
					else
						*buf = key;
					break;
				case CHAN_DOWN_BUTTON:
					if (in & 0xff00)
						*buf = DOWN_BUTTON;
					else
						*buf = key;
					break;
				default:
					fprintf(stderr,
						"unknown button: %d\n", in);
					return 0;
			}
			return 1;
		}
	}
	return 0;
}
