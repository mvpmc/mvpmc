/*
 *  Copyright (C) 2006-2007, Jon Gettler
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

static int sockfd = -1;
static int config_started = 0;
static char default_ssid[128];
static char default_wep[128];
static ti_ssid_list_t slist;
static int verbose = 0;

static int initialized = 0;

static int
read_vpd(void)
{
	int fd, i = 0;
	short *mtd;
	char *vpd, *network, *wep;
	char path[64];

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

	network = vpd+0x2008;
	wep = vpd+0x2035;

	if (network[0] != '\0') {
		if (verbose)
			printf("Default wireless network: '%s'\n", network);
		strcpy(default_ssid, network);
	} else {
		if (verbose)
			printf("No default wireless network found in flash\n");
	}

	if (wep[0] != '\0') {
		if (verbose)
			printf("WEP key found!\n");
		strcpy(default_wep, wep);
	} else {
		if (verbose)
			printf("WEP key not found!\n");
	}

	if (vpd[0x3000] != '\0') {
		if (verbose)
			printf("Default server: %d.%d.%d.%d\n",
			       vpd[0x3000], vpd[0x3001],
			       vpd[0x3002], vpd[0x3003]);
	} else {
		if (verbose)
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

	if (verbose)
		printf("device initialized!\n");

	return 0;
}

static int
start_config_manager(int fd, char *name)
{
	ti_dev_t dev;
	unsigned long buf[256];

	memset(&dev, 0, sizeof(dev));
	strcpy(dev.device, name);
	dev.arg2 = 0x0022381c;
	dev.arg3 = 0x2;
	dev.arg4 = 0x4;
	dev.ptr = (void*)0x1;

	if (ioctl(fd, SIOCDEVPRIVATE, &dev) != 0) {
		if (errno == EALREADY) {
			if (verbose)
				printf("config manager already running!\n");
			return 0;
		}
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}

	if (verbose)
		printf("config manager started!\n");

	config_started = 1;

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

	return 0;
}

static int
up_device(int fd, char *name)
{
	ti_dev_t dev;
	unsigned long buf[256];

	if (verbose)
		printf("Bringing up %s...\n", name);

	memset(&dev, 0, sizeof(dev));
	memset(&buf, 0, sizeof(buf));
	strcpy(dev.device, DEVNAME);
	dev.arg2 = 0x00224420;
	dev.arg3 = 0x1;
	dev.arg4 = 0x75;
	dev.ptr = (void*)buf;

	if (ioctl(sockfd, SIOCDEVPRIVATE+1, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE+1)");
		return -1;
	}

	memset(&dev, 0, sizeof(dev));
	memset(&buf, 0, sizeof(buf));
	strcpy(dev.device, DEVNAME);
	dev.arg2 = 0x00222018;
	dev.arg3 = 0x2;
	dev.arg4 = 0x24;
	dev.ptr = (void*)buf;

	buf[0] = strlen(default_ssid);
	strcpy((char*)(&buf[1]), default_ssid);

	if (verbose)
		printf("Using SSID: '%s'\n", default_ssid);

	if (ioctl(sockfd, SIOCDEVPRIVATE, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}

	memset(&dev, 0, sizeof(dev));
	memset(&buf, 0, sizeof(buf));
	strcpy(dev.device, DEVNAME);
	dev.arg2 = 0x00224420;
	dev.arg3 = 0x1;
	dev.arg4 = 0x75;
	dev.ptr = (void*)buf;

	if (ioctl(sockfd, SIOCDEVPRIVATE+1, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE+1)");
		return -1;
	}

	memset(&dev, 0, sizeof(dev));
	strcpy(dev.device, DEVNAME);
	dev.arg2 = 0x00223028;
	dev.arg3 = 0x1;
	dev.arg4 = 0x4;

	if (ioctl(sockfd, SIOCDEVPRIVATE+1, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE+1)");
		return -1;
	}

#if 0
	if (verbose) {
		printf("Signal strength: %ddb\n", (int)dev.ptr);
		printf("Signal strength: %p\n", dev.ptr);
	}
#endif

	return 0;
}

static void
wep_copy(char *buf, char *key)
{
	int i, len;

	len = strlen(key) / 2;

	for (i=0; i<len; i++) {
		int h, l;

		h = key[i*2];
		l = key[i*2+1];

		if ((h>='0') && (h<='9')) {
			h = h - '0';
		} else {
			h = h - 'A' + 10;
		}
		if ((l>='0') && (l<='9')) {
			l = l - '0';
		} else {
			l = l - 'A' + 10;
		}

		buf[i] = (h << 4) | l;
	}
}

static int
up_device_wep(int fd, char *name)
{
	ti_dev_t dev;
	unsigned long buf[256];

	if (verbose)
		printf("Bringing up WEP-enabled %s...\n", name);

	if (init_device(sockfd, DEVNAME) < 0)
		return -1;

	if (start_config_manager(sockfd, DEVNAME) < 0)
		return -1;

	memset(&dev, 0, sizeof(dev));
	strcpy(dev.device, DEVNAME);
	dev.arg2 = 0x00223410;
	dev.arg3 = 0x2;
	dev.arg4 = 0x4;

	if (ioctl(sockfd, SIOCDEVPRIVATE, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}

	memset(&dev, 0, sizeof(dev));
	strcpy(dev.device, DEVNAME);
	dev.arg2 = 0x00223488;
	dev.arg3 = 0x2;
	dev.arg4 = 0x4;

	if (ioctl(sockfd, SIOCDEVPRIVATE, &dev) != 0) {
		/*
		 * XXX: this error seems normal
		 */
#if 0
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
#endif
	}

	memset(&dev, 0, sizeof(dev));
	memset(&buf, 0, sizeof(buf));
	strcpy(dev.device, DEVNAME);
	dev.arg2 = 0x00223404;
	dev.arg3 = 0x2;
	dev.arg4 = 0x2c;
	dev.ptr = (void*)buf;

	buf[0] = 0x2c;
	buf[1] = 0x80000000;
	buf[2] = 0xd;
	wep_copy((char*)(buf+3), default_wep);

	if (ioctl(sockfd, SIOCDEVPRIVATE, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}

	memset(&dev, 0, sizeof(dev));
	memset(&buf, 0, sizeof(buf));
	strcpy(dev.device, DEVNAME);
	dev.arg2 = 0x00222018;
	dev.arg3 = 0x2;
	dev.arg4 = 0x24;
	dev.ptr = (void*)buf;

	buf[0] = strlen(default_ssid);
	strcpy((char*)(&buf[1]), default_ssid);

	if (verbose)
		printf("Using SSID: '%s'\n", default_ssid);

	if (ioctl(sockfd, SIOCDEVPRIVATE, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}

	memset(&dev, 0, sizeof(dev));
	memset(&buf, 0, sizeof(buf));
	strcpy(dev.device, DEVNAME);
	dev.arg2 = 0x00224420;
	dev.arg3 = 0x1;
	dev.arg4 = 0x75;
	dev.ptr = (void*)buf;
	strcpy((char*)(&buf[0]), default_ssid);

	if (ioctl(sockfd, SIOCDEVPRIVATE+1, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE+1)");
		return -1;
	}

	memset(&dev, 0, sizeof(dev));
	memset(&buf, 0, sizeof(buf));
	strcpy(dev.device, DEVNAME);
	dev.arg2 = 0x00224420;
	dev.arg3 = 0x1;
	dev.arg4 = 0x75;
	dev.ptr = (void*)buf;

	if (ioctl(sockfd, SIOCDEVPRIVATE+1, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE+1)");
		return -1;
	}

	return 0;
}

int
get_ssid_list(int fd, char *name)
{
	ti_dev_t dev;
	int n;

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
	memset(&slist, 0, sizeof(slist));
	strcpy(dev.device, name);
	dev.arg2 = 0x0022200c;
	dev.arg3 = 0x1;
	dev.arg4 = 0x2710;
	dev.ptr = (void*)&slist;

	if (ioctl(fd, SIOCDEVPRIVATE, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}

	n = slist.count;

	if ((n > 0) && verbose)
		printf("Found %d wireless networks!\n", n);

	return n;
}

static int
init(void)
{
	struct ifreq ifr;
	ti_dev_t dev;

	if (initialized) {
		return 0;
	}

	if (read_vpd() < 0) {
		return -1;
	}

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
	dev.arg2 = 0x00222020;

	if (ioctl(sockfd, SIOCDEVPRIVATE, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}

	memset(&dev, 0, sizeof(dev));
	strcpy(dev.device, DEVNAME);
	dev.arg2 = 0x00222c20;

	if (ioctl(sockfd, SIOCDEVPRIVATE, &dev) != 0) {
		perror("ioctl(SIOCDEVPRIVATE)");
		return -1;
	}

	initialized = 1;

	return 0;
}

int
tiwlan_probe(tiwlan_ssid_t *ssid, int max)
{
	int i, n;

	if (init() < 0) {
		return -1;
	}

	if ((n=get_ssid_list(sockfd, DEVNAME)) < 0) {
		return -1;
	}

	if (max < n) {
		n = max;
	}

	for (i=0; i<n; i++) {
		strncpy(ssid[i].name, slist.ssid[i].name,
			sizeof(ssid[i].name));
	}

	return i;
}

int
tiwlan_enable(char *ssid, int with_wep)
{
	if (init() < 0) {
		return -1;
	}

	if (ssid) {
		strcpy(default_ssid, ssid);
	}

	if ((default_wep[0] == '\0') || (with_wep == 0)) {
		return up_device(sockfd, DEVNAME);
	} else {
		return up_device_wep(sockfd, DEVNAME);
	}
}

int
tiwlan_disable(void)
{
	return -1;
}

int
tiwlan_add_wep(tiwlan_ssid_t *ssid, char *wep)
{
	return -1;
}

int
tiwlan_signal(void)
{
	char buf[256];
	int fd;
	int strength = -1;

	if ((fd=open("/proc/tiwlan", O_RDONLY)) >= 0) {
		read(fd, buf, sizeof(buf));
		strength = strtoul(buf, NULL, 0);
		close(fd);
	}

	return strength;
}
