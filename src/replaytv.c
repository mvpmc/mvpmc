/*
 *  Copyright (C) 2004, Jon Gettler
 *  http://mvpmc.sourceforge.net/
 *
 *  Code based on ReplayPC:
 *    Copyright (C) 2002 Matthew T. Linehan and John Todd Larason
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

/*
 * WARNING: This is simply a proof-of-concept hack!
 *
 * This code seems to work with DVArchive 3.1.  I've been told that it does
 * not work with series 5000 ReplayTVs.  I assume the replaypc library needs
 * to be fixed.
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <cmyth.h>

#include "mvpmc.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

static pthread_t read_thread;
static pthread_t write_thread;

static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static mvpw_menu_item_attr_t item_attr = {
	.selectable = 1,
	.fg = MVPW_BLACK,
	.bg = MVPW_LIGHTGREY,
	.checkbox_fg = MVPW_GREEN,
};

extern int fd_audio, fd_video;
extern demux_handle_t *handle;

int running_replaytv = 0;

static int playing = 0;

static void
write_start(void *arg)
{
	struct timespec ts;
	int nput = 0, len;
	int alen, vlen, sbl;
	unsigned char *buf, *sb;
	unsigned long *b;
	int i;

	printf("write thread is pid %d\n", getpid());

	sleep(1);

	while (1) {
		alen = demux_write_audio(handle, fd_audio);
		vlen = demux_write_video(handle, fd_video);

		if ((alen == 0) && (vlen == 0))
			usleep(1000);

		pthread_testcancel();
	}
}

static void GetMpgCallback(unsigned char * buf, size_t len, void * vd)
{
	int nput = 0, n;
	int alen, vlen;
	unsigned char *newbuf;
	int x;

	while (nput < len) {
		n = demux_put(handle, buf+nput, len-nput);
		if (n > 0)
			nput += n;
		else
			usleep(1000);

		pthread_testcancel();
	}
}

void GetMpgFile(char *IPAddress, char *FileName, int ToStdOut) 
{
    char pathname[256];
    int i;

    if (strlen(FileName) + strlen("/Video/") + 1 > sizeof pathname) {
        fprintf(stderr, "Filename too long\n");
        exit(-1);
    }

    sprintf(pathname, "/Video/%s", FileName);

    //Tell the user we're up to something
    fprintf(stderr, "Retrieving /Video/%s...\n", FileName); 

    //Send the request for the file
    i = hfs_do_chunked(GetMpgCallback, NULL, IPAddress, 75,
                       "readfile",
                       "pos", "0",
                       "name", pathname,
                       NULL);
    
    //all done, cleanup as we leave 
    fprintf(stderr, "\nDone.\n"); 
} 

static void*
read_start(void *arg)
{
	printf("read thread is pid %d\n", getpid());

	GetMpgFile(replaytv_server, (char*)arg, 0);

	return NULL;
}

static void
select_callback(mvp_widget_t *widget, char *item, void *key)
{
	int i;

	mvpw_hide(widget);
	av_move(0, 0, 0);
	mvpw_show(root);
	mvpw_expose(root);
	mvpw_focus(root);
	
	av_play();
	demux_reset(handle);

	pthread_create(&read_thread, NULL, read_start, (void*)item);
	pthread_create(&write_thread, NULL, write_start, NULL);

	playing = 1;
}

void GetDir(mvp_widget_t *widget, char *IPAddress)
{
    int     i;
    time_t  TimeStamp=0;            //A Unix Timestamp
    char * data, * cur, * e;

    fprintf(stderr, "[Directory Listing of /Video...]\n"); 

    i = hfs_do_simple(&data, IPAddress,
                      "ls",
                      "name", "\"/Video\"",
                      NULL);
    if (i != 0) {
        fprintf(stderr, "hfs_do_simple returned error code %d.\n", i);
        exit(-1);
    }

	item_attr.select = select_callback;
    
    cur = data;
    while (cur && *cur) {
        e = strchr(cur, '\n');
        if (e)
            *e = '\0';
        
            fprintf(stdout, "'%s'\n", cur);
	    if (strstr(cur, ".mpg") != 0)
		mvpw_add_menu_item(widget, cur, (void*)0, &item_attr);

        if (e)
            cur = e + 1;
        else
            cur = NULL;
    }

    //all done, cleanup as we leave 
    fprintf(stderr, "\n[End of listing.]\n");

    free(data);
}

int
replaytv_update(mvp_widget_t *widget)
{
	running_replaytv = 1;

	mvpw_show(root);
	mvpw_expose(root);

	mvpw_clear_menu(widget);

	GetDir(widget, replaytv_server);

	return 0;
}

void
replaytv_stop(void)
{
	running_replaytv = 0;

	if (playing) {
		pthread_cancel(read_thread);
		pthread_cancel(write_thread);

		playing = 0;
	}
}
