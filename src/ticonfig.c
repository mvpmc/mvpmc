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

#include "mvp_av.h"

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
} ti_ssid_ret_t;

static struct option opts[] = {
	{ "help", no_argument, 0, 'h' },
	{ 0, 0, 0, 0 }
};

static void
print_help(char *prog)
{
	printf("Usage: %s <options>\n", prog);
}

static int
read_vpd(void)
{
	int fd, i = 0;
	short *mtd;
	char *vpd, *network;
	char path[64];
	av_mode_t video;
	av_video_aspect_t ratio;

	if ((fd=open("/proc/mtd", O_RDONLY)) > 0) {
		FILE *f = fdopen(fd, "r");
		char line[64];
		/* read the header */
		fgets(line, sizeof(line), f);
		/* read each mtd entry */
		while (fgets(line, sizeof(line), f) != NULL) {
			if (strstr(line, " VPD") != NULL) {
				break;
			}
			i++;
		}
		fclose(f);
		close(fd);
	}

	if (i != 2) {
		fprintf(stderr, "not running on a wireless MVP!\n");
		return -1;
	}

	snprintf(path, sizeof(path), "/dev/mtd%d", i);

	if ((fd=open(path, O_RDONLY)) < 0)
		return -1;

	if ((mtd=malloc(65536)) == NULL) {
		close(fd);
		return -1;
	}

	if (read(fd, mtd, 65536) != 65536) {
		close(fd);
		return -1;
	}

	vpd = (char*)mtd;

	close(fd);

	video = mtd[2119];
	ratio = mtd[2125];

	switch (video) {
	case AV_MODE_NTSC:
		printf("TV Mode: NTSC\n");
		break;
	case AV_MODE_PAL:
		printf("TV Mode: PAL\n");
		break;
	default:
		printf("TV Mode: Unknown\n");
		break;
	}

	switch (ratio) {
	case AV_TV_ASPECT_4x3:
		printf("TV Aspect Ratio: 4x3\n");
		break;
	case AV_TV_ASPECT_16x9:
		printf("TV Aspect Ratio: 16x9\n");
		break;
	default:
		printf("TV Aspect Ratio: Unknown\n");
		break;
	}

	network = vpd+0x2008;

	if (network[0] != '\0') {
		printf("Default wireless network: '%s'\n", network);
	} else {
		printf("No default wireless network found in flash\n");
	}

	if (vpd[0x3000] != '\0') {
		printf("Default server: %d.%d.%d.%d\n",
		       vpd[0x3000], vpd[0x3001], vpd[0x3002], vpd[0x3003]);
	} else {
		printf("No default server found in flash\n");
	}

	free(mtd);

	return 0;
}
static int
init_device(int fd, char *name)
{
	ti_dev_t dev;
	ti_name_t ssid;

	memset(&dev, 0, sizeof(dev));
	memset(&ssid, 0, sizeof(ssid));

	strcpy(dev.device, name);
	dev.arg2 = 0x00223834;
	dev.arg3 = 0x2;
	dev.arg4 = 0xd;
	dev.ptr = &ssid;

	if (ioctl(fd, SIOCDEVPRIVATE, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}

	printf("device initialized!\n");

	return 0;
}

static int
start_config_manager(int fd, char *name)
{
	ti_dev_t dev;

	memset(&dev, 0, sizeof(dev));
	strcpy(dev.device, name);
	dev.arg2 = 0x0022381c;
	dev.arg3 = 0x2;
	dev.arg4 = 0x4;
	dev.ptr = (void*)0x1;

	if (ioctl(fd, SIOCDEVPRIVATE, &dev) != 0) {
		if (errno == EALREADY) {
			printf("config manager already running!\n");
			return 0;
		}
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}

	printf("config manager started!\n");

	return 0;
}

static int
get_ssid_list(int fd, char *name)
{
	ti_dev_t dev;
	ti_ssid_ret_t ssid;
	int n, i;

	memset(&dev, 0, sizeof(dev));
	strcpy(dev.device, name);
	dev.arg2 = 0x00222c20;

	if (ioctl(fd, SIOCDEVPRIVATE, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}

	memset(&dev, 0, sizeof(dev));
	strcpy(dev.device, name);
	dev.arg2 = 0x00222c1c;
	dev.arg3 = 0x1;
	dev.arg4 = 0x4;

	if (ioctl(fd, SIOCDEVPRIVATE, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}


	memset(&dev, 0, sizeof(dev));
	memset(&ssid, 0, sizeof(ssid));
	strcpy(dev.device, name);
	dev.arg2 = 0x0022200c;
	dev.arg3 = 0x1;
	dev.arg4 = 0x2710;
	dev.ptr = (void*)&ssid;

	if (ioctl(fd, SIOCDEVPRIVATE, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}

	n = ssid.count;

	printf("Found %d wireless networks!\n", n);

	for (i=0; i<n; i++) {
		printf("Found SSID: '%s'\n", ssid.ssid[i].name);
	}


	return 0;
}

static int
init(void)
{
	struct ifreq ifr;
	int sockfd;
	unsigned long buf[256];
	ti_dev_t dev;

	strncpy(ifr.ifr_name, DEVNAME, IFNAMSIZ);

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket()");
		return -1;
	}

	if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
		perror("ioctl(SIOCGIFFLAGS)");
		return -1;
	}

	if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0) {
		perror("ioctl(SIOCSIFFLAGS)");
		return -1;
	}

	if (init_device(sockfd, DEVNAME) < 0)
		return -1;

	if (start_config_manager(sockfd, DEVNAME) < 0)
		return -1;

	memset(&dev, 0, sizeof(dev));
	strcpy(dev.device, DEVNAME);
	dev.arg2 = 0x00223818;
	dev.arg3 = 0x1;
	dev.arg4 = 0x4;
	dev.ptr = (void*)0x0;

	if (ioctl(sockfd, SIOCDEVPRIVATE+1, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE+1)");
		return -1;
	}

	memset(&dev, 0, sizeof(dev));
	strcpy(dev.device, DEVNAME);
	dev.arg2 = 0x00222018;
	dev.arg3 = 0x2;
	dev.arg4 = 0x24;
	dev.ptr = (void*)buf;

	buf[0] = 0x00000008;
	memcpy(buf+1, "NON-SSID", 8);
	buf[3] = 0x00000000;
	buf[4] = 0x00000000;
	buf[5] = 0x00000000;
	buf[6] = 0x00000000;
	buf[7] = 0x00000000;

	if (ioctl(sockfd, SIOCDEVPRIVATE, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}

	if (get_ssid_list(sockfd, DEVNAME) < 0)
		return -1;

	close(sockfd);

	return 0;
}

int
main(int argc, char **argv)
{
	int c;
	int opt_index;

	while ((c=getopt_long(argc, argv, "h", opts, &opt_index)) != -1) {
		switch (c) {
		case 'h':
			print_help(argv[0]);
			exit(0);
			break;
		default:
			print_help(argv[0]);
			exit(1);
			break;
		}
	}

	if (read_vpd() != 0) {
		fprintf(stderr, "VPD read failed!\n");
		exit(1);
	}

	if (init() != 0) {
		fprintf(stderr, "initialization failed!\n");
		exit(1);
	}

	printf("success!\n");

	return 0;
}
