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
#include <sys/types.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <cmyth.h>

#include "rtvlib.h"
#include "mvpmc.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

#define MAX_RTVS 10

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

static int playing = 0;


// Top level replayTV structure
static rtv_device_t rtv_top[10];

static void rtv_init(void) 
{
   static int rtv_initialized = 0;
   if ( rtv_initialized == 0 ) {
      memset(rtv_top, 0, sizeof(rtv_top));
      rtv_init_lib();
      rtv_initialized = 1;
   }
}

static rtv_device_t *get_rtv_device_struct(char* ipaddr, int *new) {
   int x;

   *new = 0;
   for ( x=0; x < MAX_RTVS; x++ ) {
      if ( rtv_top[x].device.ipaddr == NULL ) {
         *new = 1;
         return(&(rtv_top[x])); //New entry
      }
      if ( strcmp(rtv_top[x].device.ipaddr, ipaddr) == 0 ) {
         return(&(rtv_top[x])); //Existing entry
      }
   }
   return(NULL);
}

#if 0
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
#endif

static void GetMpgCallback(unsigned char * buf, size_t len, void * vd)
{
        int nput = 0, n;

//        printf(".");
        while (nput < len) {
                n = demux_put(handle, buf+nput, len-nput);
                pthread_cond_broadcast(&video_cond);
                if (n > 0)
                        nput += n;
                else
                        usleep(1000);

                pthread_testcancel();
        }
        //JBH: Fixme
        //We should call free here but somehow it doesn't matter
        //If we do call free here then mvpmc cores when the OSD button is pressed.
        //free(buf);
}

void GetMpgFile(char *IPAddress, char *FileName, int ToStdOut) 
{
    rtv_device_t *rtv;  
    char pathname[256];
    int i, new_entry;

    if (strlen(FileName) + strlen("/Video/") + 1 > sizeof pathname) {
        fprintf(stderr, "Filename too long\n");
        exit(-1);
    }

    sprintf(pathname, "/Video/%s", FileName);

    //Tell the user we're up to something
    fprintf(stderr, "Retrieving /Video/%s...\n", FileName); 

    rtv = get_rtv_device_struct(replaytv_server, &new_entry);

    //Send the request for the file
    i = rtv_read_file( &(rtv->device), pathname, 0, 0, 32, 0, GetMpgCallback, NULL );

    //all done, cleanup as we leave 
    fprintf(stderr, "\nDone.\n"); 
} 

static void*
read_start(void *arg)
{
	printf("read thread is pid %d\n", getpid());

	pthread_mutex_init(&mutex, NULL);
	pthread_mutex_lock(&mutex);

	GetMpgFile(replaytv_server, (char*)arg, 0);

	return NULL;
}

static void
old_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	mvpw_hide(widget);
	av_move(0, 0, 0);
	mvpw_show(root);
	mvpw_expose(root);
	mvpw_focus(root);
	
	av_play();
	demux_reset(handle);

	pthread_create(&read_thread, NULL, read_start, (void*)item);

	playing = 1;
}

static void
select_callback(mvp_widget_t *widget, char *item, void *key)
{
   rtv_show_export_t  *show = (rtv_show_export_t*)key;

	mvpw_hide(widget);
	av_move(0, 0, 0);
	mvpw_show(root);
	mvpw_expose(root);
	mvpw_focus(root);

   printf("Playing file: %s\n", show->file_name);
 	
	av_play();
	demux_reset(handle);

	pthread_create(&read_thread, NULL, read_start, (void*)show->file_name);

	playing = 1;
}

void GetDir(mvp_widget_t *widget, char *IPAddress)
{
    rtv_device_t       *rtv;  
    rtv_fs_filelist_t  *filelist;
    int                 i, new_entry;

    rtv = get_rtv_device_struct(replaytv_server, &new_entry);
    
    i = rtv_get_filelist( &(rtv->device), "/Video", 0, &filelist );
    if ( i == 0 ) {
       rtv_print_file_list(filelist, 0);
    }
    else {
       fprintf(stderr, "rtv_get_filelist failed: error code %d.\n", i);
       exit(-1);      
    }
    
    item_attr.select = old_select_callback;
    
    for ( i=0; i < filelist->num_files; i++) {
       if (strstr(filelist->files[i].name, ".mpg") != 0) {
          mvpw_add_menu_item(widget, filelist->files[i].name, (void*)0, &item_attr);
       }
    }
    rtv_free_file_list(&filelist);
}

void GetGuide(mvp_widget_t *widget, char *IPAddress)
{
    rtv_device_t       *rtv;  
    rtv_guide_export_t *guide;
    int                 x, new_entry, rc;

    rtv = get_rtv_device_struct(replaytv_server, &new_entry);
    if ( new_entry == 1 ) {
       fprintf(stderr, "**ERROR: Failed to get existing RTV device info for %s\n", replaytv_server);
       return;
    }
    guide = &(rtv->guide);
    if ( (rc = rtv_get_guide_snapshot( &(rtv->device), NULL, guide)) != 0 ) {
       fprintf(stderr, "**ERROR: Failed to get Show Guilde for %s\n", replaytv_server);
       return;
    }
    rtv_print_guide(guide);
    item_attr.select = select_callback;
 
    for ( x=0; x < guide->num_rec_shows; x++ ) {
       char title_episode[255];
       snprintf(title_episode, 254, "%s: %s", guide->rec_show_list[x].title, guide->rec_show_list[x].episode);
       printf("%d:  %s\n", x, title_episode);
       mvpw_add_menu_item(widget, title_episode, &(guide->rec_show_list[x]), &item_attr);
    }

    //all done, cleanup as we leave 
    fprintf(stderr, "\n[End of Show:Episode Listing.]\n");
}

int
replaytv_update(mvp_widget_t *widget)
{
   rtv_device_t      *rtv;  
   rtv_device_info_t *devinfo;
   int                rc, new_entry;

	running_replaytv = 1;
   rtv_init();

   printf( "\nGetting replaytv (%s) device info...\n", replaytv_server);
   rtv = get_rtv_device_struct(replaytv_server, &new_entry);
   if ( new_entry ) {
      printf("Got New RTV Device Struct Entry\n");
   }
   else {
      printf("Found Existing RTV Device Struct Entry\n");
   }
   devinfo = &(rtv->device);

   if ( (rc = rtv_get_device_info(replaytv_server, devinfo)) != 0 ) {
      printf("**ERROR: Failed to get RTV Device Info.\n");
      return 0;
   } 
   rtv_print_device_info(devinfo);



	mvpw_show(root);
	mvpw_expose(root);

	mvpw_clear_menu(widget);

   if ( atoi(rtv->device.modelNumber) == 4999 ) {
      //DVArchive hack since guide doesn't work yet
      GetDir(widget, replaytv_server);
   }
   else {
      GetGuide(widget, replaytv_server);
   }
	return 0;
}

void
replaytv_stop(void)
{
	running_replaytv = 0;

	if (playing) {
		pthread_cancel(read_thread);

		playing = 0;
	}
}
