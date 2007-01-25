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
#include <string.h>
#include <sys/types.h> 
#include <libgen.h>

void print_help(char *prog);

#define VPD_NVRAM_TFTP 0x3000

           
void print_help(char *prog)
{
    printf("MediaMVP VPD Read functions\n");
	printf("Usage: %s [-ht]\n", basename(prog));
	printf("\t-h   print help\n");
	printf("\t-t   TFTP=dongle.bin server ip\n");
}           

int vpdread_main(int argc, char **argv)
{
	int fd, i = 0, found = 0;
	char ip[4];
	char path[64];
    FILE *f;
    int c;
    int option=-1;

    if (argc==1) {
        print_help(argv[0]);
        exit(1);
    }

	while ((c=getopt(argc, argv, "ht")) != -1) {
		switch (c) {
		case 'h':
			print_help(argv[0]);
			exit(0);
			break;
        case 't':
            option = c;
			break;
		default:
			print_help(argv[0]);
			exit(1);
			break;
		}
	}


	if ((fd=open("/proc/mtd", O_RDONLY)) > 0) {
		f = fdopen(fd, "r");
		char line[64];
		/* read the header */
		fgets(line, sizeof(line), f);
		/* read each mtd entry */
		while (fgets(line, sizeof(line), f) != NULL) {
			if (strstr(line, " VPD") != NULL) {
				found = 1;
				break;
			}
			i++;
		}
		fclose(f);
		close(fd);
	}

	if (!found) {
		return -1;
	}

	snprintf(path, sizeof(path), "/dev/mtd%d", i);
    switch (option) {
        case 't':
            
            /* note it can be 255.255.255.255.255 on older mvp's */

            if ((fd=open(path, O_RDONLY)) < 0)
                return -1;

            lseek(fd,VPD_NVRAM_TFTP,SEEK_SET);
            if (read(fd, ip, 4) != 4) {
                close(fd);
                return -1;
            }
            close(fd);
            f = fopen("/etc/tftp.config","w"); 
            fprintf(f,"TFTP=%d.%d.%d.%d\n",ip[0],ip[1],ip[2],ip[3]);
            fclose (f);
            break;
        default:
            /* only one option so far */
            break;
    }
	return 0;
}
