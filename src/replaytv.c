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
#include "replaytv.h"

//+********************************************************************************************
//
// For hauppauge remote to character mappings see:
//    workarea/microwindows-0.90/src/drivers/kbd_irm.
//
//+********************************************************************************************

#define MAX_IP_SZ (50)

//+***********************************************
//    externals 
//+***********************************************
extern pthread_t       video_write_thread;
extern pthread_t       audio_write_thread;
extern int             fd_audio, fd_video;
extern demux_handle_t *handle;


//+***********************************************
//    types
//+***********************************************
#if 0
#define DBGPRT(x...) printf(x)
#else
#define DBGPRT(x...)
#endif

typedef enum rtv_menu_level_t
{
   RTV_NO_MENU       = 0,
   RTV_DEVICE_MENU   = 1,
   RTV_BROWSER_MENU  = 2
} rtv_menu_level_t;

typedef enum rtv_play_state_t
{
   RTV_VID_STOPPED    = 1,
   RTV_VID_PLAYING    = 2,
   RTV_VID_PAUSED     = 3,
   RTV_VID_ABORT_PLAY = 4
} rtv_play_state_t;

typedef struct rtv_video_state_t 
{
   rtv_play_state_t  play_state;
   rtv_show_export_t show_p;     // currently playing show record
   __u64             pos;        // mpg file position
} rtv_video_state_t;


//+***********************************************
//    locals
//+***********************************************

// replaytv menu level
static volatile rtv_menu_level_t  rtv_level = RTV_NO_MENU;

// currently selected replaytv device
static volatile rtv_device_t *current_rtv_device = NULL;

// state of currently playing file
static volatile rtv_video_state_t rtv_video_state = {
   .play_state = RTV_VID_STOPPED,
};

// set true when ssdp discovery is performed 
static int rtvs_discovered     = 0;

// number of static addresses passed at cmdline 
static int num_cmdline_ipaddrs = 0;

// hack for static ipaddresses passed from commandline
static char rtv_ip_addrs[MAX_RTVS][MAX_IP_SZ + 1];

// threading
static pthread_t       read_thread;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// screen info
static mvpw_screen_info_t scr_info;

// default menu attributes
static mvpw_menu_attr_t  rtv_default_menu_attr = {
	.font = 0,
	.fg = MVPW_BLACK,
	.hilite_fg = MVPW_WHITE,
	.hilite_bg = MVPW_DARKGREY2,
	.title_fg = MVPW_WHITE,
	.title_bg = MVPW_BLUE,
};

// default list item attributes. (colors control unselected list items)
static mvpw_menu_item_attr_t rtv_default_item_attr = {
   .selectable = 1,
   .fg = MVPW_BLACK,      //   .fg = 0xff8b8b7a, //LIGHTCYAN4
   .bg = MVPW_LIGHTGREY,  //   .bg = 0xff1a1a1a, //GREY10
   .checkbox_fg = MVPW_GREEN,
};

// device menu list item attributes
static mvpw_menu_item_attr_t device_menu_item_attr = {
   .selectable = 1,
   .fg = MVPW_BLACK,
   .bg = MVPW_LIGHTGREY,
   .checkbox_fg = MVPW_GREEN,
};

// rtv_popup widget attributes
static mvpw_menu_attr_t rtv_popup_attr = {
   .font = 0,
   .fg = MVPW_BLACK,
   .hilite_fg = MVPW_BLACK,
   .hilite_bg = MVPW_WHITE,
   .title_fg = MVPW_BLACK,
   .title_bg = MVPW_LIGHTGREY,
};

// rtv_popup widget menu attributes
static mvpw_menu_item_attr_t rtv_popup_item_attr = {
   .selectable = 1,
   .fg = MVPW_GREEN,
   .bg = MVPW_BLACK,
};

// show description text attributes
static mvpw_text_attr_t description_attr = {
   .wrap = 1,
   .justify = MVPW_TEXT_LEFT,
   .margin = 4,
   .font = 0,
   .fg = MVPW_WHITE,
};

// show info window text attributes
static mvpw_text_attr_t rtv_info_attr = {
   .wrap = 1,
   .justify = MVPW_TEXT_LEFT,
   .margin = 4,
   .font = 0,
   .fg = MVPW_WHITE,
};

// program info window text attributes
static mvpw_text_attr_t rtv_program_attr = {
   .wrap = 0,
   .justify = MVPW_TEXT_LEFT,
   .margin = 6,
   .font = 0,
   .fg = MVPW_CYAN,
};

// show description window text attributes
static mvpw_text_attr_t rtv_description_attr = {
   .wrap = 1,
   .justify = MVPW_TEXT_LEFT,
   .margin = 6,
   .font = 0,
   .fg = MVPW_WHITE,
};

// osd text attributes
static mvpw_text_attr_t rtv_osd_display_attr = {
   .wrap = 0,
   .justify = MVPW_TEXT_LEFT,
   .margin = 6,
   .font = 0,
   .fg = MVPW_WHITE,
};

// splash window text attributes
static mvpw_text_attr_t splash_attr = {
   .wrap = 1,
   .justify = MVPW_TEXT_LEFT,
   .margin = 6,
   .font = 0,
   .fg = MVPW_GREEN,
};

// widgets
static mvp_widget_t *rtv_date;
static mvp_widget_t *rtv_description;
static mvp_widget_t *rtv_channel;
static mvp_widget_t *rtv_record;
static mvp_widget_t *rtv_popup;
static mvp_widget_t *rtv_info;
static mvp_widget_t *rtv_shows_widget;
static mvp_widget_t *rtv_episodes_widget;
static mvp_widget_t *rtv_freespace_widget;
static mvp_widget_t *replaytv_logo;

static mvp_widget_t *rtv_program_widget;
static mvp_widget_t *rtv_osd_program;
static mvp_widget_t *rtv_osd_description;
static mvp_widget_t *replaytv_device_menu;
static mvp_widget_t *replaytv_browser;
static mvp_widget_t *splash;


//************************************************************************************************************************
//******************         local functions             *****************************************************************
//************************************************************************************************************************

// strip_spaces
//
static char* strip_spaces(char *str)
{
   while ( str[0] == ' ' ) 
      str++;
   while ( str[strlen(str)-1] == ' ' )
      str[strlen(str)-1] = '\0';
   return(str);
}

// parse_ip_init_str
//
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
      strncpy(rtv_ip_addrs[num_cmdline_ipaddrs], cur, MAX_IP_SZ );
      printf("RTV IP [%d]: %s\n", num_cmdline_ipaddrs, rtv_ip_addrs[num_cmdline_ipaddrs]);
      num_cmdline_ipaddrs++;
      cur = next;
   }
   return(0);
}


// rtv_abort_read()
// abort rtv_read_file() stream
//
static int rtv_abort_read(void)
{
   if ( (rtv_video_state.play_state != RTV_VID_STOPPED)    &&
        (rtv_video_state.play_state != RTV_VID_ABORT_PLAY)  ) {
      rtv_video_state.play_state = RTV_VID_ABORT_PLAY;
      pthread_join(read_thread, NULL);
      pthread_kill(video_write_thread, SIGURG);
      pthread_kill(audio_write_thread, SIGURG);
      av_reset();
      rtv_video_state.play_state = RTV_VID_STOPPED;
   }
   return(0);
}

// get_mpeg_callback()
// rtvlib callback for rtv_read_file()
//
static int get_mpeg_callback(unsigned char *buf, size_t len, void *vd)
{
   int nput = 0, n;
   
   while (nput < len) {
      n = demux_put(handle, buf+nput, len-nput);
      pthread_cond_broadcast(&video_cond);
      if (n > 0)
         nput += n;
      else
         usleep(1000);
      if ( rtv_video_state.play_state == RTV_VID_ABORT_PLAY ) {
         return(1);
      }
   }
   return(0);
}


// get_mpeg_file()
// start a rtv mpeg file read stream
//
static void get_mpeg_file(rtv_device_t *rtv, char *filename, int ToStdOut) 
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
   i = rtv_read_file( &(rtv->device), pathname, 0, 0, 0, get_mpeg_callback, NULL );
   
   //all done, cleanup as we leave 
   fprintf(stderr, "\nDone.\n"); 
} 


// thread_read_start()
// setup rtv mpeg file read stream
//
static void* thread_read_start(void *arg)
{
   printf("read thread is pid %d\n", getpid());
   
   pthread_mutex_init(&mutex, NULL);
   pthread_mutex_lock(&mutex);
   
   get_mpeg_file(current_rtv_device, (char*)arg, 0);
   pthread_exit(NULL);
   return NULL;
}


// dirlist_select_callback
//
static void dirlist_select_callback(mvp_widget_t *widget, char *item, void *key)
{
   mvpw_hide(widget);
   mvpw_hide(replaytv_logo);
   av_move(0, 0, 0);
   mvpw_show(root);
   mvpw_expose(root);
   mvpw_focus(root);
   
   rtv_abort_read();
   
   printf("Playing file: %s\n", item);
   av_play();
   demux_reset(handle);
   
   pthread_create(&read_thread, NULL, thread_read_start, (void*)item);
   rtv_video_state.play_state = RTV_VID_PLAYING;
}

// guide_select_callback
//
static void guide_select_callback(mvp_widget_t *widget, char *item, void *key)
{
   rtv_show_export_t  *show = (rtv_show_export_t*)key;
   
   mvpw_hide(widget);
   mvpw_hide(replaytv_logo);
   av_move(0, 0, 0);
   mvpw_show(root);
   mvpw_expose(root);
   mvpw_focus(root);
   
   rtv_abort_read();
   
   rtv_print_show(show, 0);
   printf("Playing file: %s\n", show->file_name);  
   av_play();
   demux_reset(handle);
   
   pthread_create(&read_thread, NULL, thread_read_start, (void*)show->file_name);
   rtv_video_state.play_state = RTV_VID_PLAYING;
}

// rtv_get_video_dir_lis
//
static void rtv_get_video_dir_list(mvp_widget_t *widget, rtv_device_t *rtv)
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
   
   rtv_default_item_attr.select = dirlist_select_callback;
   
   for ( i=0; i < filelist->num_files; i++) {
      if (strstr(filelist->files[i].name, ".mpg") != 0) {
         mvpw_add_menu_item(widget, filelist->files[i].name, (void*)i, &rtv_default_item_attr);
      }
   }
   rtv_free_file_list(&filelist);
}

// rtv_get_guide
//
static void rtv_get_guide(mvp_widget_t *widget, rtv_device_t *rtv)
{
   rtv_fs_volume_t    *volinfo;
   rtv_guide_export_t *guide;
   int                 x, rc;
   
   // Verify we can access the Video directory. If not the RTV's time is probably off by more
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
   //rtv_print_guide(guide);
   rtv_default_item_attr.select = guide_select_callback;
   
   for ( x=0; x < guide->num_rec_shows; x++ ) {
      char title_episode[255];
      snprintf(title_episode, 254, "%s: %s", guide->rec_show_list[x].title, guide->rec_show_list[x].episode);
      printf("%d:  %s\n", x, title_episode);
      mvpw_add_menu_item(widget, title_episode, &(guide->rec_show_list[x]), &rtv_default_item_attr);
   }
   
   //all done, cleanup as we leave 
   fprintf(stderr, "\n[End of Show:Episode Listing.]\n");
}


// rtv_device_select_callback
//
static void rtv_device_select_callback(mvp_widget_t *widget, char *item, void *key)
{
   rtv_device_t *rtv = (rtv_device_t*)key;  
   char          buf[256];
   
   mvpw_hide(widget);
   mvpw_clear_menu(replaytv_browser);
   snprintf(buf, sizeof(buf), " %s", rtv->device.name);
   mvpw_set_menu_title(replaytv_browser, buf);
   
   current_rtv_device = rtv;
   
   // We have to do SSDP (discovery) on DVArchive to put it into RTV 4K/5K mode
   // If a static address was used instead of discovery then just list the dir
   //
   if ( (atoi(rtv->device.modelNumber) == 4999) && (rtvs_discovered == 0) ) {
      rtv_get_video_dir_list(replaytv_browser, rtv);
   }
   else {
      rtv_get_guide(replaytv_browser, rtv);
   }
   
   mvpw_show(replaytv_browser);
   mvpw_focus(replaytv_browser);
   rtv_level = RTV_BROWSER_MENU;
   return;
}

// take command line ip addresses and get devide info for each
//
static int bogus_discover(void) 
{
   rtv_device_t      *rtv;  
   int                rc, x;
   
   for ( x=0; x < num_cmdline_ipaddrs; x++ ) {
      printf( "\nGetting replaytv (%s) device info...\n", rtv_ip_addrs[x]);
      
      if ( (rc = rtv_get_device_info(rtv_ip_addrs[x], NULL, &rtv)) != 0 ) {
         printf("Failed to get RTV Device Info. Retrying...\n");
         
         // Hack for dvarchive failing on first attempt is not auto-discovered 
         if ( (rc = rtv_get_device_info(rtv_ip_addrs[x], NULL, &rtv)) != 0 ) {
            printf("**ERROR: Unable to get RTV Device Info for: %s. Giving up\n", rtv_ip_addrs[x]);
            return 0;
         } 
      } 
      //rtv_print_device_info(&(rtv->device));
   }
   rtv_print_device_list();
   return(0);
}


// bound as replaytv device selection menu callback
// 
static void rtv_device_menu_callback(mvp_widget_t *widget, char key)
{
   if (key == 'E') {
      running_replaytv = 0;
      replaytv_back_to_mvp_main_menu();
   }
}

// build show program info
//
static int rtv_proginfo(char *buf, int size)
{
   snprintf(buf, size,
            "Title: %s\n"
            "Subtitle: %s\n"
            "Description: %s\n"
            "Start: %s\n"
            "End: %s\n"
            "Category: %s\n"
            "Channel: %s\n"
            "Series ID: %s\n"
            "Program ID: %s\n"
            "Stars: %s\n",
            "Bogus Title",
            "Bogus Episode",
            "This is a description",
            "0:0:0",
            "12:12:12",
            "My Category",
            "ESPN",
            "Heelo Series",
            "I'm a prog id",
            "***");
   
   return 0;
}

static void rtv_popup_key_callback(mvp_widget_t *widget, char key)
{
   // menu & back/exit buttons
   //
   if (key == 'M') {
      mvpw_hide(rtv_popup);
   }
   
   if (key == 'E') {
      mvpw_hide(rtv_popup);
   }
}

// Bound from rtv_browser_key_callback() for pressing menu buttion while in show list
//
static void rtv_popup_select_callback(mvp_widget_t *widget, char *item, void *key)
{
   int which = (int)key;
   char buf[1024];
   
   switch (which) {
   case 0:
      printf("trying to forget recording\n");
//    if ((mythtv_delete() == 0) && (mythtv_forget() == 0)) {
      if ( 1 ) {
         mvpw_hide(rtv_popup);
         rtv_level = 0;
//         replaytv_update(replaytv_browser);
      }
      break;
   case 1:
      printf("trying to delete recording\n");
//    if (mythtv_delete() == 0) {
      if ( 1 ) {
         mvpw_hide(rtv_popup);
         rtv_level = 0;
//         replaytv_update(replaytv_browser);
      }
      break;
   case 2:
      printf("show info...\n");
      rtv_proginfo(buf, sizeof(buf));
      mvpw_set_text_str(rtv_info, buf);
      mvpw_show(rtv_info);
      mvpw_focus(rtv_info);
      break;
   case 3:
      mvpw_hide(rtv_popup);
      break;
   }
}


// Bound as callback for replaytv info window.
// Currently just closes the show info window.
//
static void
rtv_info_key_callback(mvp_widget_t *widget, char key)
{
   mvpw_hide(widget);
}


// initialize on-screen-display stuff.
// This stuff seem to control how text menu's operate
//
int replaytv_osd_init(void)
{
   mvp_widget_t *widget, *contain;
   int h, x, y;
   
   
   rtv_osd_display_attr.font = fontid;
   h = mvpw_font_height(rtv_osd_display_attr.font) + (2 * rtv_osd_display_attr.margin);
   
   rtv_program_attr.font = fontid;
   rtv_description_attr.font = fontid;
   x = scr_info.cols - 475;
   y = scr_info.rows - 125;
   contain = mvpw_create_container(NULL, x, y,
                                   400, h*3, 0x80000000, 0, 0);
   rtv_program_widget = contain;
   widget = mvpw_create_text(contain, 0, 0, 400, h, 0x80000000, 0, 0);
   mvpw_set_text_attr(widget, &rtv_program_attr);
   mvpw_set_text_str(widget, "");
   mvpw_show(widget);
   rtv_osd_program = widget;
   widget = mvpw_create_text(contain, 0, 0, 400, h*2, 0x80000000, 0, 0);
   mvpw_set_text_attr(widget, &rtv_description_attr);
   mvpw_set_text_str(widget, "");
   mvpw_show(widget);
   rtv_osd_description = widget;
   mvpw_attach(rtv_osd_program, rtv_osd_description, MVPW_DIR_DOWN);
   
   return(0);
}


// called when back/exit pressed from show browser
//
static int rtv_back_to_device_menu(mvp_widget_t *widget)
{
   mvpw_hide(replaytv_browser);
   mvpw_show(replaytv_device_menu);
   mvpw_focus(replaytv_device_menu);
   rtv_level = RTV_DEVICE_MENU;
   return(0);
}


// Bound as replaytv_browser widget callback
//
static void rtv_browser_key_callback(mvp_widget_t *widget, char key)
{
   // Back/Exit
   //
   if (key == 'E') {
      rtv_back_to_device_menu(widget);
   }
   
   // Menu button
   //
   if ( key == 'M' ) {
      printf("replaytv popup menu\n");
      mvpw_clear_menu(rtv_popup);
      rtv_popup_item_attr.select = rtv_popup_select_callback;
      mvpw_add_menu_item(rtv_popup,
                         "Delete, but allow future recordings",
                         (void*)0, &rtv_popup_item_attr);
      mvpw_add_menu_item(rtv_popup, "Delete",
                         (void*)1, &rtv_popup_item_attr);
      mvpw_add_menu_item(rtv_popup, "Show Info",
                         (void*)2, &rtv_popup_item_attr);
      mvpw_add_menu_item(rtv_popup, "Cancel",
                         (void*)3, &rtv_popup_item_attr);
      mvpw_menu_hilite_item(rtv_popup, (void*)3);
      mvpw_show(rtv_popup);
      mvpw_focus(rtv_popup);
   }
}

//+**************************************************************************
//+******************* Public functions *************************************
//+**************************************************************************

// replaytv_init
// parse commandline, initialize library, etc...
//
int replaytv_init(char *init_str) 
{
   char *cur = init_str;
   
   static int rtv_initialized = 0;
   if ( rtv_initialized == 1 ) {
      return(0);
   }
   
   memset(rtv_ip_addrs, 0, sizeof(rtv_ip_addrs));
   rtv_init_lib();
   
   printf("replaytv init string: %s\n", init_str);
   if ( strchr(cur, '=') == NULL ) {
      // Assume just a single parm that is either an ip address or "discover"
      if ( strncmp(cur, "disc", 4) != 0 ) {
         strncpy(rtv_ip_addrs[0], cur, MAX_IP_SZ );
         num_cmdline_ipaddrs = 1;
      }
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
            if ( strncmp(val, "disc", 4) != 0 ) {
               parse_ip_init_str(val);
            }
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


// discover devices and update device menu.
//
int replaytv_device_update(void)
{
   mvp_widget_t      *widget = replaytv_device_menu;
   int                h, w, x, y, idx, rc;
   char               buf[128];
   rtv_device_list_t *rtv_list;
   
   running_replaytv = 1;
   
   mvpw_show(root);
   mvpw_expose(root);
   mvpw_clear_menu(widget);
   mvpw_set_menu_title(widget, "Device List");
   
   // build discovery splash window
   //
   snprintf(buf, sizeof(buf), "Discovering ReplayTV devices...");
   splash_attr.font = fontid;
   h = (mvpw_font_height(splash_attr.font) + (2 * splash_attr.margin)) * 2;
   w = mvpw_font_width(fontid, buf) + 8;
   
   x = (scr_info.cols - w) / 2;
   y = (scr_info.rows - h) / 2;
   
   splash = mvpw_create_text(NULL, x, y, w, h, 0x80000000, 0, 0);
   mvpw_set_text_attr(splash, &splash_attr);
   
   mvpw_set_text_str(splash, buf);
   mvpw_show(splash);
   mvpw_event_flush();
   
   // do discovery
   //
   if ( num_cmdline_ipaddrs == 0 ) {
      rtv_free_devices();
      rc = rtv_discover(4000, &rtv_list);
      if ( rc == 0 ) {
         rtvs_discovered = 1;
         rtv_print_device_list();
      }
      else {
         printf("\n***ERROR: RTV Discovery Failed\n\n");
      }
   }
   else {
      bogus_discover();
      sleep(1);
   }
   mvpw_destroy(splash);
   
   // Add discovered devices to the list
   //
   device_menu_item_attr.select = rtv_device_select_callback;
   for ( idx=0; idx < rtv_devices.num_rtvs; idx++ ) {
      if ( rtv_devices.rtv[idx].device.name != NULL ) {
         mvpw_add_menu_item(widget, rtv_devices.rtv[idx].device.name, &(rtv_devices.rtv[idx]), &device_menu_item_attr);
      }
   }
   
   rtv_level = RTV_DEVICE_MENU;
   return 0;
}

// replaytv_show_device_menu
//
int replaytv_show_device_menu(void)
{
   mvpw_show(replaytv_logo);
   mvpw_show(replaytv_device_menu);
   mvpw_focus(replaytv_device_menu);
   return(0);
}

// Hide all widgets
//
int replaytv_hide_device_menu(void)
{
   mvpw_hide(replaytv_logo);
   mvpw_hide(replaytv_device_menu);
   return(0);
}

// called from video.c when back/exit or stop pressed
//
void replaytv_back_from_video(void)
{
   rtv_abort_read();
   mvpw_show(replaytv_logo);
   mvpw_show(replaytv_browser);
   mvpw_focus(replaytv_browser);
   rtv_level = RTV_BROWSER_MENU;
}

// replaytv gui init
//
int replay_gui_init(void)
{
   char               logo_file[128];
   mvpw_widget_info_t info;
   mvpw_image_info_t  iid;
   mvpw_widget_info_t wid, wid2;
   int x, y, w, h;
   
   mvpw_get_screen_info(&scr_info);
   rtv_default_menu_attr.font = fontid;
   
   // int replaytv logo image
   //
   snprintf(logo_file, sizeof(logo_file), "%s/replaytv1_rotate.png", imagedir);
   if (mvpw_get_image_info(logo_file, &iid) < 0) {
      return -1;
   }
   replaytv_logo = mvpw_create_image(NULL, 50, 25, iid.width, iid.height, 0, 0, 0);
   mvpw_set_image(replaytv_logo, logo_file);
   
   // int device selection menu
   //
   replaytv_device_menu = mvpw_create_menu(NULL, 50+iid.width, 30,
                                           scr_info.cols-120-iid.width, scr_info.rows-210,
                                           0xff808080, 0xff606060, 2);
   
   mvpw_set_menu_attr(replaytv_device_menu, &rtv_default_menu_attr);
   mvpw_attach(replaytv_logo, replaytv_device_menu, MVPW_DIR_RIGHT);   
   mvpw_set_key(replaytv_device_menu, rtv_device_menu_callback);
   
   // init show browser 
   //
   replaytv_browser = mvpw_create_menu(NULL, 50, 30,
                                       scr_info.cols-120-iid.width, scr_info.rows-190,
                                       0xff808080, 0xff606060, 2);
   
   mvpw_set_menu_attr(replaytv_browser, &rtv_default_menu_attr);
   mvpw_attach(replaytv_logo, replaytv_browser, MVPW_DIR_RIGHT);   
   mvpw_set_key(replaytv_browser, rtv_browser_key_callback);
   
   // show popup menu
   //
   w = 300;
   h = 150;
   x = (scr_info.cols - w) / 2;
   y = (scr_info.rows - h) / 2;
   
   rtv_popup = mvpw_create_menu(NULL, x, y, w, h,
                                MVPW_BLACK, MVPW_GREEN, 2);
   
   rtv_popup_attr.font = fontid;
   mvpw_set_menu_attr(rtv_popup, &rtv_popup_attr);
   
   mvpw_set_menu_title(rtv_popup, "Recording Menu");
   mvpw_set_bg(rtv_popup, MVPW_BLACK);
   
   mvpw_set_key(rtv_popup, rtv_popup_key_callback);
   
   // show info description window
   //
   description_attr.font = fontid;
   h = mvpw_font_height(description_attr.font) +
      (2 * description_attr.margin);
   
   rtv_channel = mvpw_create_text(NULL, 0, 0, 350, h,
                                  MVPW_BLACK, 0, 0);
   rtv_date = mvpw_create_text(NULL, 0, 0, 350, h,
                               MVPW_BLACK, 0, 0);
   rtv_description = mvpw_create_text(NULL, 0, 0, 350, h*3,
                                      MVPW_BLACK, 0, 0);
   rtv_record = mvpw_create_text(NULL, 0, 0, 350, h,
                                 MVPW_BLACK, 0, 0);
   
   mvpw_set_text_attr(rtv_channel, &description_attr);
   mvpw_set_text_attr(rtv_date, &description_attr);
   mvpw_set_text_attr(rtv_description, &description_attr);
   mvpw_set_text_attr(rtv_record, &description_attr);
   
   mvpw_get_widget_info(replaytv_logo, &wid);
   mvpw_get_widget_info(replaytv_browser, &wid2);
   mvpw_moveto(rtv_channel, wid.x, wid2.y+wid2.h);
   mvpw_get_widget_info(rtv_channel, &wid2);
   mvpw_moveto(rtv_date, wid.x, wid2.y+wid2.h);
   mvpw_get_widget_info(rtv_date, &wid2);
   mvpw_moveto(rtv_description, wid.x, wid2.y+wid2.h);
   
   mvpw_attach(rtv_channel, rtv_record, MVPW_DIR_RIGHT);
   
   mvpw_get_widget_info(rtv_channel, &info);
   shows_widget = mvpw_create_text(NULL, info.x, info.y,
                                   300, h, 0x80000000, 0, 0);
   episodes_widget = mvpw_create_text(NULL, 50, 80,
                                      300, h, 0x80000000, 0, 0);
   freespace_widget = mvpw_create_text(NULL, 50, 80,
                                       300, h, 0x80000000, 0, 0);
   mvpw_set_text_attr(shows_widget, &description_attr);
   mvpw_set_text_attr(episodes_widget, &description_attr);
   mvpw_set_text_attr(freespace_widget, &description_attr);
   
   mvpw_attach(shows_widget, episodes_widget, MVPW_DIR_DOWN);
   mvpw_attach(episodes_widget, freespace_widget, MVPW_DIR_DOWN);
   
   w = scr_info.cols - 100;
   h = scr_info.rows - 40;
   x = (scr_info.cols - w) / 2;
   y = (scr_info.rows - h) / 2;
   rtv_info = mvpw_create_text(NULL, x, y, w, h, 0, 0, 0);
   mvpw_set_key(rtv_info, rtv_info_key_callback);
   
   rtv_info_attr.font = fontid;
   mvpw_set_text_attr(rtv_info, &rtv_info_attr);
   
   mvpw_raise(replaytv_browser);
   mvpw_raise(replaytv_device_menu);
   mvpw_raise(rtv_info);
   mvpw_raise(rtv_popup);
   return(0);
}


//+**************************************************************************
//+******************* Not Used         *************************************
//+**************************************************************************

#if 0

// generic menu attributes
static mvpw_menu_attr_t rtv_default_menu_attr = {
   .font = 0,
   .fg = MVPW_BLACK,
//   .fg        = 0xff8b8b7a,    //LIGHTCYAN4
   .hilite_fg = MVPW_WHITE,
//   .hilite_fg = 0xff20a5da,    //GOLDENROD
//   .hilite_fg = 0xffcda66c,    //SKYBLUE
//   .hilite_fg = 0xff000000,  //BLACK
   .hilite_bg = MVPW_DARKGREY2,
//   .hilite_bg = 0xff0b86b8,    //DARKGOLDENROD
   .title_fg = MVPW_WHITE,
//   .title_fg  = 0xff0b86b8,    //DARKGOLDENROD
   .title_bg = MVPW_BLUE,
//   .title_bg  = 0xff8b0000,    //DARKBLUE
};


// device selection menu attributes
static mvpw_menu_item_attr_t rtv_menu_item_attr = {
   .selectable = 1,
   .fg = MVPW_WHITE,
//   .fg = 0xffcd5969,        //SLATEBLUE3
//   .fg = 0xffcda66c,        //SKYBLUE
//   .fg = 0xff8b8b7a,    //LIGHTCYAN4
   .bg = 0,
//   .bg = 0xff1a1a1a, //GREY10
   .checkbox_fg = MVPW_GREEN,
};



int
rtv_update(mvp_widget_t *widget)
{
   char buf[64];
   unsigned int total, used;
   
   
   mvpw_show(root);
   mvpw_expose(root);
   
   video_clear();
   mvpw_set_idle(NULL);
   mvpw_set_timer(root, NULL, 0);
   
// if (control == NULL)
//    mythtv_init(mythtv_server, -1);
   
   add_osd_widget(rtv_program_widget, OSD_PROGRAM, 1, NULL);
   
   mvpw_set_menu_title(widget, "ReplayTV");
   mvpw_clear_menu(widget);
   //add_shows(widget);
   
   snprintf(buf, sizeof(buf), "Total shows: %d", 5);
   mvpw_set_text_str(shows_widget, buf);
   snprintf(buf, sizeof(buf), "Total episodes: %d", 10);
   mvpw_set_text_str(episodes_widget, buf);
   
// if (cmyth_conn_get_freespace(control, &total, &used) == 0) {
   if ( 1 ) {
      total=1000000;
      used = 50000;
      snprintf(buf, sizeof(buf), "Diskspace: %d/%d  %5.2f%%",
               used, total, ((float)used/total)*100.0);
      mvpw_set_text_str(rtv_freespace_widget, buf);
   }
   
   mvpw_hide(rtv_channel);
   mvpw_hide(rtv_date);
   mvpw_hide(rtv_description);
   
   mvpw_show(rtv_shows_widget);
   mvpw_show(rtv_episodes_widget);
   mvpw_show(rtv_freespace_widget);
   
   return 0;
}

#endif

