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
#include <pthread.h>
#include <assert.h>

#include "mvp_demux.h"

#define BSIZE	(256*1024)

static demux_handle_t *handle;

static pthread_t thread;

static volatile int read_done = 0;

volatile int audio_read = 0;
volatile int video_read = 0;

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

static void*
write_start(void *arg)
{
	int alen, vlen, aget, vget, tot = 0, atot = 0, vtot = 0;
	unsigned int ah = 0, vh = 0;
	char abuf[BSIZE], vbuf[BSIZE];
	int x = 0;

	sleep(1);

	while (1) {
		alen = 0;
		vlen = 0;

		aget = rand() % sizeof(abuf);
		vget = rand() % sizeof(vbuf);
		if (aget > 0)
			alen = demux_get_audio(handle, abuf, aget);
		if (vget > 0)
			vlen = demux_get_video(handle, vbuf, vget);

		if (alen < 0)
			printf("alen = %d\n", alen);
		if (vlen < 0)
			printf("vlen = %d\n", vlen);

		assert(alen >= 0);
		assert(vlen >= 0);

		if ((vlen == 0) && (alen == 0) &&
		    ((aget > 0) && (vget > 0)) &&
		    (read_done) && (x++ > 16))
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

	audio_read = atot;
	video_read = vtot;

	return NULL;
}

int
main(int argc, char **argv)
{
	int fd, len, n, tot, atot, vtot, i;
	int rget = 0;
	unsigned int ah, vh;
	char buf[BSIZE];
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

	pthread_create(&thread, NULL, write_start, NULL);

	ah = vh = 0;
	len = n = 0;
	tot = atot = vtot = 0;
	while (1) {
		do {
			rget = rand() % sizeof(buf);
		} while (rget == 0);

		len = read(fd, buf, rget);

		if (len <= 0)
			break;

		n = 0;
		while (n < len)
			n += demux_put(handle, buf+n, len-n);
	}

	read_done = 1;

	sleep(2);

	attr = demux_get_attr(handle);

	printf("Demuxer stats:\n");
	printf("\taudio: %d frames, %d bytes   full %d empty %d\n",
	       attr->audio.stats.frames, attr->audio.stats.bytes,
	       attr->audio.stats.full_count, attr->audio.stats.empty_count);
	printf("\tvideo: %d frames, %d bytes   full %d empty %d\n",
	       attr->video.stats.frames, attr->video.stats.bytes,
	       attr->video.stats.full_count, attr->video.stats.empty_count);
	
	i = 0;
	while ((i < 32) && (attr->spu[i].frames)) {
		printf("\tsubtitle stream %d: %d frames, %d bytes\n",
		       i, attr->spu[i].frames, attr->spu[i].bytes);
		i++;
	}

	if (attr->audio.stats.bytes != audio_read)
		return -1;
	if (attr->video.stats.bytes != video_read)
		return -1;

	return 0;
}
