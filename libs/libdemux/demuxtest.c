/*
 *  Copyright (C) 2004, Jon Gettler
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

/*
 * This test program will pass an mpeg file through the demuxer and hash
 * the audio and video output.  The point of this is as a regression test
 * to prove that changes to the demuxer do or do not change the resulting
 * audio and video output.
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "mvp_demux.h"

#define BSIZE	(256*1024)

static demux_handle_t *handle;

static unsigned int
hash(char *s, int len, unsigned int old)
{
	char *p;
	unsigned int h = old, g;

	for (p=s; len > 0; p++, len--) {
		h = (h << 4) + *p;
		if ((g = h & 0xf0000000)) {
			h = h ^ (g >> 24);
			h = h ^ g;
		}
	}

	return h;
}

int
main(int argc, char **argv)
{
	int fd, len, n, alen, vlen, aget, vget, tot, atot, vtot, i;
	int rget = 0;
	unsigned int ah, vh;
	char buf[BSIZE], abuf[BSIZE], vbuf[BSIZE];
	demux_attr_t *attr;

	srand(getpid());

	if ((fd=open(argv[1], O_RDONLY)) < 0) {
		perror(argv[1]);
		exit(1);
	}

	if ((handle=demux_init(1024*1024*4)) == NULL) {
		fprintf(stderr, "failed to initialize demuxer\n");
		exit(1);
	}

	ah = vh = 0;
	len = n = 0;
	tot = atot = vtot = 0;
	while (1) {
		if ((len-n) == 0) {
			rget = rand() % sizeof(buf);
			len = read(fd, buf, rget);
			n = 0;
			if (len >= 0)
				tot += len;
			else {
				perror("read()");
				break;
			}
		}
		n += demux_put(handle, buf+n, len-n);

		aget = rand() % sizeof(abuf);
		vget = rand() % sizeof(vbuf);
		alen = demux_get_audio(handle, abuf, aget);
		vlen = demux_get_video(handle, vbuf, vget);

		if (((len == 0) && (n == 0) && (alen == 0) && (vlen == 0)) &&
		    ((aget > 0) && (vget > 0) && (rget > 0)))
			break;

		if (alen > 0) {
			ah = hash(abuf, alen, ah);
			atot += alen;
		}
		if (vlen > 0) {
			vh = hash(vbuf, vlen, vh);
			vtot += vlen;
		}
	}

	printf("Bytes: stream %d  audio %d  video %d\n", tot, atot, vtot);
	printf("Hash: audio 0x%.8x video 0x%.8x\n", ah, vh);

	attr = demux_get_attr(handle);

	printf("Demuxer stats:\n");
	printf("\taudio: %d frames, %d bytes\n",
	       attr->audio.stats.frames, attr->audio.stats.bytes);
	printf("\tvideo: %d frames, %d bytes\n",
	       attr->video.stats.frames, attr->video.stats.bytes);
	
	i = 0;
	while ((i < 32) && (attr->spu[i].frames)) {
		printf("\tsubtitle stream %d: %d frames, %d bytes\n",
		       i, attr->spu[i].frames, attr->spu[i].bytes);
		i++;
	}

	return 0;
}
