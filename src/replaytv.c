/*
 *  Copyright (C) 2004, Jon Gettler, John Honeycutt
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
#include <signal.h>
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

extern pthread_t video_write_thread;
extern pthread_t audio_write_thread;

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

static mvpw_menu_item_attr_t device_menu_item_attr = {
   .selectable = 1,
   .fg = MVPW_BLACK,
   .bg = MVPW_LIGHTGREY,
   .checkbox_fg = MVPW_GREEN,
};

static mvpw_text_attr_t splash_attr = {
	.wrap = 1,
	.justify = MVPW_TEXT_LEFT,
	.margin = 6,
	.font = 0,
	.fg = MVPW_GREEN,
};

static mvp_widget_t      *splash;
static mvpw_screen_info_t scr_info;

extern int fd_audio, fd_video;
extern demux_handle_t *handle;

static int playing    = 0;
static int abort_read = 0;

// Top level replayTV structure
static int           num_rtv = 0;
static rtv_device_t  rtv_top[MAX_RTVS];
static rtv_device_t *current_rtv_device = NULL;

//hack
#define MAX_IP_SZ (50)
static char rtv_ip_addrs[MAX_RTVS][MAX_IP_SZ + 1];

static char* strip_spaces(char *str)
{
   while ( str[0] == ' ' ) 
      str++;
   while ( str[strlen(str)-1] == ' ' )
      str[strlen(str)-1] = '\0';
   return(str);
}

static int parse_ip_init_str(char *str)  
{
   int   done = 0;
   char *cur  = str;

   while ( !(done) ) {
      char *next; 
      
      if ( (next = strchr(cur, '/')) == NULL ) {
         done = 1;
      }
      else {
         next[0] = '\0';
         next++;
      }
      strncpy(rtv_ip_addrs[num_rtv], cur, MAX_IP_SZ );
      printf("RTV IP [%d]: %s\n", num_rtv, rtv_ip_addrs[num_rtv]);
      num_rtv++;
      cur = next;
   }
   return(0);
}

int rtv_init(char *init_str) 
{
   char *cur = init_str;

   static int rtv_initialized = 0;
   if ( rtv_initialized == 1 ) {
      return(0);
   }

   memset(rtv_top, 0, sizeof(rtv_top));
   memset(rtv_ip_addrs, 0, sizeof(rtv_ip_addrs));
   rtv_init_lib();
   
   printf("replaytv init string: %s\n", init_str);
   if ( strchr(cur, '=') == NULL ) {
      // Assume just a single parm that is an ip address
      strncpy(rtv_ip_addrs[0], cur, MAX_IP_SZ );
      num_rtv = 1;
   }
   else {
      int done = 0;
      while ( !(done) ) {
         char *val, *next_cur; 
         cur = strip_spaces(cur);
         if ( (val = strchr(cur, '=')) == NULL ) {
            break;
         }
         val[0] = '\0';
         val++;
         if ( (next_cur = strchr(val, ' ')) == NULL ) {
            done = 1;
         }
         else {
            next_cur[0] = '\0';
            next_cur++;
         }
         
         printf("------------> KEY=%s VAL=%s\n", cur, val);
         if ( strcmp(cur, "ip") == 0 ) {
            parse_ip_init_str(val);
         }
         else if ( strcmp(cur, "debug") == 0 ) {
            unsigned int dbgmask = strtoul((val), NULL, 16);
            rtv_set_dbgmask(dbgmask);
         }
         else if ( strcmp(cur, "logfile") == 0 ) {
            int rc; 
            if ( (rc = rtv_route_logs(val)) != 0 ) {
               printf("***ERROR: RTV: Failed to open logfile: %s\n", val);
            }
         }
         else {
            printf("***ERROR: RTV: Invalid option: %s\n", cur);
         }
         
         cur = next_cur;
      } //while
   }

   rtv_initialized = 1;
   return(0);
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

static int GetMpgCallback(unsigned char * buf, size_t len, void * vd)
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
                if ( abort_read == 1 ) {
                   return(1);
                }
        }
        return(0);
}

void GetMpgFile(rtv_device_t *rtv, char *filename, int ToStdOut) 
{
    char pathname[256];
    int i;

    if (strlen(filename) + strlen("/Video/") + 1 > sizeof pathname) {
        fprintf(stderr, "Filename too long\n");
        exit(-1);
    }

    sprintf(pathname, "/Video/%s", filename);

    //Tell the user we're up to something
    fprintf(stderr, "Retrieving /Video/%s...\n", filename); 

    //Send the request for the file
    i = rtv_read_file( &(rtv->device), pathname, 0, 0, 0, GetMpgCallback, NULL );

    //all done, cleanup as we leave 
    fprintf(stderr, "\nDone.\n"); 
} 

static void*
read_start(void *arg)
{
   printf("read thread is pid %d\n", getpid());

   pthread_mutex_init(&mutex, NULL);
   pthread_mutex_lock(&mutex);

   GetMpgFile(current_rtv_device, (char*)arg, 0);
   pthread_exit(NULL);
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
   
   if (playing) {
      abort_read = 1;
      running_replaytv = 0;
      pthread_join(read_thread, NULL);
      pthread_kill(video_write_thread, SIGURG);
      pthread_kill(audio_write_thread, SIGURG);
      av_reset();
      running_replaytv = 1;
   }
   
   printf("Playing file: %s\n", item);
   av_play();
   demux_reset(handle);

   abort_read = 0;
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

   if (playing) {
      abort_read = 1;
      running_replaytv = 0;
      pthread_join(read_thread, NULL);
      pthread_kill(video_write_thread, SIGURG);
      pthread_kill(audio_write_thread, SIGURG);
      av_reset();
      running_replaytv = 1;
  }

   printf("Playing file: %s\n", show->file_name);  
   av_play();
   demux_reset(handle);

   abort_read = 0;
   pthread_create(&read_thread, NULL, read_start, (void*)show->file_name);
   playing = 1;
}

void GetDir(mvp_widget_t *widget, rtv_device_t *rtv)
{
    rtv_fs_filelist_t  *filelist;
    int                 i;

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

void GetGuide(mvp_widget_t *widget, rtv_device_t *rtv)
{
   rtv_fs_volume_t    *volinfo;
   rtv_guide_export_t *guide;
   int                 x, rc;
   
   // Verfify we can access the Video directory. If not the RTV's time is probably off by more
   // than 40 seconds from ours.
   rc = rtv_get_volinfo( &(rtv->device), "/Video", &volinfo );
   if ( rc != 0 ) {
      fprintf(stderr, "**ERROR: Failed to access /Video directory for RTV %s\n", rtv->device.name);
      fprintf(stderr, "         The RTV's clock and this clock must be within 40 seconds of each other\n");
      return;
   }
   
   guide = &(rtv->guide);
   if ( (rc = rtv_get_guide_snapshot( &(rtv->device), NULL, guide)) != 0 ) {
      fprintf(stderr, "**ERROR: Failed to get Show Guilde for RTV %s\n", rtv->device.name);
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

static void
rtv_device_select_callback(mvp_widget_t *widget, char *item, void *key)
{
   rtv_device_t *rtv = (rtv_device_t*)key;  
   char          buf[256];

	mvpw_clear_menu(widget);
	snprintf(buf, sizeof(buf), "ReplayTV-%s", rtv->device.name);
	mvpw_set_menu_title(widget, buf);

   current_rtv_device = rtv;
   if ( atoi(rtv->device.modelNumber) == 4999 ) {
      //DVArchive hack since guide doesn't work yet
      GetDir(widget, rtv);
   }
   else {
      GetGuide(widget, rtv);
   }   
   return;
}

static int bogus_discover(void) 
{
   rtv_device_t      *rtv;  
   rtv_device_info_t *devinfo;
   int                rc, new_entry, x;

   for ( x=0; x < num_rtv; x++ ) {
      printf( "\nGetting replaytv (%s) device info...\n", rtv_ip_addrs[x]);
      rtv = get_rtv_device_struct(rtv_ip_addrs[x], &new_entry);
      if ( new_entry ) {
         printf("Got New RTV Device Struct Entry\n");
      }
      else {
         printf("Found Existing RTV Device Struct Entry\n");
      }
      devinfo = &(rtv->device);
      
      if ( (rc = rtv_get_device_info(rtv_ip_addrs[x], devinfo)) != 0 ) {
         printf("Failed to get RTV Device Info. Retrying...\n");
         
         // JBH: Fixme: Hack for dvarchive failing on first attempt. Need to get discovery working  
         if ( (rc = rtv_get_device_info(rtv_ip_addrs[x], devinfo)) != 0 ) {
            printf("**ERROR: Unable to get RTV Device Info for: %s. Giving up\n", rtv_ip_addrs[x]);
            return 0;
         } 
      } 
      rtv_print_device_info(devinfo);
   }
   return(0);
}

int replaytv_update(mvp_widget_t *widget)
{
   int h, w, x, y, idx;
	char buf[128];

   running_replaytv = 1;

   mvpw_show(root);
   mvpw_expose(root);
   mvpw_clear_menu(widget);

   mvpw_get_screen_info(&scr_info);

	snprintf(buf, sizeof(buf), "Discovering ReplayTV devices...");

	splash_attr.font = fontid;
	h = (mvpw_font_height(splash_attr.font) +
	     (2 * splash_attr.margin)) * 2;
	w = mvpw_font_width(fontid, buf) + 8;
   
	x = (scr_info.cols - w) / 2;
	y = (scr_info.rows - h) / 2;
   
	splash = mvpw_create_text(NULL, x, y, w, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(splash, &splash_attr);
   
   mvpw_set_text_str(splash, buf);
	mvpw_show(splash);
	mvpw_event_flush();

   sleep(1);
	mvpw_destroy(splash);

   bogus_discover();

   device_menu_item_attr.select = rtv_device_select_callback;
   for ( idx=0; idx < num_rtv; idx++ ) {
      if ( rtv_top[idx].device.name != NULL ) {
         mvpw_add_menu_item(widget, rtv_top[idx].device.name, &(rtv_top[idx]), &device_menu_item_attr);
      }
   }

   return 0;
}

void
replaytv_stop(void)
{
   printf("In replaytv_stop\n");
   running_replaytv = 0;

   if (playing) {
      abort_read = 1;
      pthread_join(read_thread, NULL);
      pthread_join(read_thread, NULL);
      pthread_kill(video_write_thread, SIGURG);
      pthread_kill(audio_write_thread, SIGURG);
      av_reset();
      playing    = 0;
      abort_read = 0;
   }
}
