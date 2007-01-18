/*
 *  Copyright (C) 2006, Jon Gettler
 *  http://www.mvpmc.org/
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
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>

#include "tiwlan.h"

#define DEVNAME		"eth1"

typedef struct {
	unsigned char device[16];
	unsigned long arg2;
	unsigned long arg3;
	unsigned long arg4;
	void *ptr;
} ti_dev_t;

typedef struct {
	unsigned int len;
	char name[128];
} ti_name_t;

typedef struct {
	unsigned int unknown[3];
	unsigned int len;
	char name[36];
	unsigned int unknown2[27];
} ti_ssid_t;

typedef struct {
	unsigned int count;
	ti_ssid_t ssid[32];
} ti_ssid_list_t;

static char default_wep[128];
static int verbose = 0;

static struct option opts[] = {
	{ "help", no_argument, 0, 'h' },
	{ "probe", no_argument, 0, 'p' },
	{ "signal", no_argument, 0, 'S' },
	{ "ssid", required_argument, 0, 's' },
	{ "verbose", no_argument, 0, 'v' },
	{ "wep", required_argument, 0, 'w' },
	{ 0, 0, 0, 0 }
};

static void
print_help(char *prog)
{
	printf("Usage: %s <options>\n", prog);
	printf("\t-h      \tprint this help\n");
	printf("\t-p      \tprobe for wireless networks\n");
	printf("\t-s ssid \tSSID to use\n");
	printf("\t-S      \tdisplay signal strength\n");
	printf("\t-v      \tverbose\n");
	printf("\t-w key  \tWEP key\n");
}

static void
show_signal(void)
{
	char buf[256];
	int strength;

	strength = tiwlan_signal();

	if (strength >= 0) {
		char *msg;
		/*
		 * The following ranges are a SWAG.
		 */
		if (strength < 50) {
			msg = "excellent";
		} else if (strength < 75) {
			msg = "good";
		} else if (strength < 90) {
			msg = "fair";
		} else {
			msg = "weak";
		}
		snprintf(buf, sizeof(buf), "%d - %s", strength, msg);
	} else {
		strcpy(buf, "No Signal");
	}

	printf("Signal Strength: %s\n", buf);
}

int
main(int argc, char **argv)
{
	int c, n, i;
	int opt_index;
	int do_probe = 0, do_signal = 0;
	char *ssid = NULL, *key = NULL;
	int with_wep = -1;
	tiwlan_ssid_t *ssid_list;

	while ((c=getopt_long(argc, argv,
			      "hps:Sw:v", opts, &opt_index)) != -1) {
		switch (c) {
		case 'h':
			print_help(argv[0]);
			exit(0);
			break;
		case 'p':
			do_probe = 1;
			break;
		case 's':
			ssid = strdup(optarg);
			break;
		case 'S':
			do_signal = 1;
			break;
		case 'w':
			if (strcasecmp(optarg, "off") == 0) {
				with_wep = 0;
			} else if (strcasecmp(optarg, "on") == 0) {
				with_wep = 1;
			} else {
				with_wep = 1;
				key = strdup(optarg);
			}
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			print_help(argv[0]);
			exit(1);
			break;
		}
	}

	if (do_signal) {
		show_signal();
		exit(0);
	}

	if (key) {
		if (strlen(key) == 13) {
			fprintf(stderr, "64-bit WEP not supported!\n");
			exit(1);
		}
		if (strlen(key) != 26) {
			fprintf(stderr, "incorrect WEP key length!\n");
			exit(1);
		}
		strcpy(default_wep, key);
	}

	if (do_probe) {
		ssid_list = (tiwlan_ssid_t*)(malloc(sizeof(*ssid_list)*16));
		if ((n=tiwlan_probe(ssid_list, 16)) < 0) {
			fprintf(stderr, "SSID probe failed!\n");
			exit(1);
		}
		for (i=0; i<n; i++) {
			printf("Found SSID: '%s'\n", ssid_list[i].name);
		}
		exit(0);
	}

	if (tiwlan_enable(ssid, with_wep) < 0) {
		fprintf(stderr, "device bringup failed!\n");
		exit(1);
	}

	return 0;
}
