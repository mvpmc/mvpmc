/*
 *  Copyright (C) 2004, John Honeycutt
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
#include <semaphore.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>

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
#define NUM_EPISODE_LINES (5)
#define RTV_XFER_CHUNK_SZ (1024 * 32)
#define RTV_VID_Q_SZ (2)
#define MAX_FILENAME_LEN (256)

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

typedef enum rtv_popup_menu_action_t
{
   RTV_POPUP_CANCEL   = 0,
   RTV_POPUP_PLAY_FB  = 1,
   RTV_POPUP_PLAY     = 2,
   RTV_POPUP_DELETE   = 3
} rtv_popup_menu_action_t;

typedef enum rtv_ndx_version_t 
{
   RTV_NDX_22      = 22,
   RTV_NDX_30      = 30,
} rtv_ndx_version_t;

typedef struct rtv_ndx_info_t 
{
   rtv_ndx_version_t     ver;                 //ndx file version
   unsigned int          file_sz;             //file size
   unsigned int          hdr_sz;              //file header size
   unsigned int          rec_sz;              //record size
   unsigned int          num_rec;             //number of records in file
   unsigned int          rec_cnt_to_load;     //number of records to load into memory
   unsigned int          com_skip_ok;         //handle commercial skip
   unsigned int          start_rec_num;       //starting record number for block in memory
   unsigned int          recs_in_mem;         //number of records in memory
   rtv_http_resp_data_t  file_chunk;          //current chunk of ndx file in memory
   char                  filename[MAX_FILENAME_LEN];
} rtv_ndx_info_t;

typedef struct rtv_evt_info_t 
{
   rtv_ndx_version_t  ver;
   unsigned int       hdr_sz;
   unsigned int       num_rec;
   unsigned int       chunk_sz;
   unsigned int       file_pos;
   unsigned int       file_sz;
   char              *cur_chunk_p;
   char               filename[MAX_FILENAME_LEN];
} rtv_evt_info_t;

typedef enum rtv_play_state_t
{
   RTV_VID_STOPPED    = 1,
   RTV_VID_PLAYING    = 2,
   RTV_VID_PAUSED     = 3,
   RTV_VID_ABORT_PLAY = 4
} rtv_play_state_t;

typedef struct rtv_video_queue_t 
{
   char *buffer[RTV_VID_Q_SZ];    //buffer pointer
   int   data_off[RTV_VID_Q_SZ];  //data start offset in buffer
   int   data_len[RTV_VID_Q_SZ];  //data len 
   int   write_pos;
   int   read_pos;
   char *last_read_buf;
   pthread_mutex_t queue_lock;
   sem_t           sem_not_empty;
   sem_t           sem_not_full;
} rtv_video_queue_t;

typedef struct rtv_selected_show_state_t 
{
   volatile rtv_play_state_t  play_state;
   rtv_ndx_info_t             ndx_info;
   rtv_evt_info_t             evt_info;
   int                        processing_jump_input;  // capturing key input for jump
   const rtv_show_export_t   *show_p;                 // currently selected show record
   __u64                      pos;                    // mpg file position
   unsigned int               chunk_offset;           // data offset within mpeg chunk. (Used 
                                                      // when jumping to start on a GOP)
   rtv_video_queue_t          vidq;                   // video stream buffer queue
} rtv_selected_show_state_t;


//+***********************************************
//   callback functions
//+***********************************************
static int rtv_open(void);
static int rtv_video_queue_read(char**, int);
static long long rtv_seek(long long, int);
static long long rtv_size(void);
static void rtv_notify(mvp_notify_t);
static int rtv_video_key(char);


video_callback_t replaytv_functions = {
   .open      = rtv_open,
   .read      = NULL,
   .read_dynb = rtv_video_queue_read,
   .seek      = rtv_seek,
   .size      = rtv_size,
   .notify    = rtv_notify,
	.key       = rtv_video_key,
};

//+***********************************************
//    locals
//+***********************************************

// replaytv menu level
static volatile rtv_menu_level_t  rtv_level = RTV_NO_MENU;

// currently selected replaytv device
static volatile rtv_device_t *current_rtv_device = NULL;

// state of currently playing file
static rtv_selected_show_state_t rtv_video_state;

// set true when ssdp discovery is performed 
static int rtvs_discovered = 0;

// number of static addresses passed at cmdline 
static int num_cmdline_ipaddrs = 0;

// hack for static ipaddresses passed from commandline
static char rtv_ip_addrs[MAX_RTVS][MAX_IP_SZ + 1];

// threading
static pthread_t       rtv_stream_read_thread;
static sem_t           sem_mvp_readthread_idle;

// screen info
static mvpw_screen_info_t scr_info;

// default menu attributes
static mvpw_menu_attr_t  rtv_default_menu_attr = {
	.font = 0,
	.fg = MVPW_BLACK,
	.hilite_fg = MVPW_WHITE,
	.hilite_bg = MVPW_DARKGREY2,
	.title_fg = MVPW_WHITE,
	.title_bg = 0xff701919, //MIDNIGHTBLUE,
};

// show browser popup window menu attributes
static mvpw_menu_attr_t rtv_popup_attr = {
	.font = 0,
	.fg = MVPW_BLACK,
	.hilite_fg = MVPW_BLACK,
	.hilite_bg = MVPW_WHITE,
	//.title_fg = MVPW_BLACK,
	//.title_fg = 0xff701919,    //MIDNIGHTBLUE
	.title_fg = MVPW_WHITE,
	//.title_bg = MVPW_LIGHTGREY,
	//.title_bg = 0xff08658b,    //DARKGOLDENROD4
	.title_bg = 0xff701919, //MIDNIGHTBLUE,
};

// show browser popup window list item attributes
static mvpw_menu_item_attr_t rtv_popup_item_attr = {
	.selectable = 1,
	.fg = MVPW_GREEN,
	.bg = MVPW_BLACK,
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

// device description window text attributes
static mvpw_text_attr_t rtv_device_descr_attr = {
   .wrap = 1,
   .justify = MVPW_TEXT_LEFT,
   .margin = 0,
   .font = 0,
   .fg = 0xff8b8b7a,    //LIGHTCYAN4,
};

static mvpw_graph_attr_t discspace_graph_attr = {
	.min = 0,
	.max = 100,
   .fg = 0xff2626cd, //FIREBRICK3
//	.fg = 0xff701919, //MIDNIGHTBLUE
};

// show episode description window text attributes
static mvpw_text_attr_t rtv_episode_descr_attr = {
   .wrap = 1,
   .justify = MVPW_TEXT_LEFT,
   .margin = 0,
   .font = 0,
   .fg = 0xff8b8b7a,    //LIGHTCYAN4,
};

// splash window text attributes
static mvpw_text_attr_t splash_attr = {
   .wrap = 1,
   .justify = MVPW_TEXT_LEFT,
   .margin = 6,
   .font = 0,
   .fg = MVPW_GREEN,
};

// OSD program info attributes
static mvpw_text_attr_t rtv_osd_show_title_attr = {
	.wrap = 0,
	.justify = MVPW_TEXT_LEFT,
	.margin = 0,
	.font = 0,
	.fg = MVPW_CYAN,
};

// OSD program info attributes
static mvpw_text_attr_t rtv_osd_show_desc_attr = {
	.wrap = 1,
	.justify = MVPW_TEXT_LEFT,
	.margin = 0,
	.font = 0,
	.fg = MVPW_WHITE,
};

// OSD program info attributes
static mvpw_text_attr_t rtv_message_window_attr = {
   .wrap = 1,
   .justify = MVPW_TEXT_LEFT,
   .margin = 6,
   .font = 0,
   .fg = MVPW_GREEN,
};

// numeric keypad jump_to_time attributes
static mvpw_text_attr_t rtv_jump_to_time_attr = {
	.wrap = 1,
	.justify = MVPW_TEXT_LEFT,
	.margin = 0,
	.font = 0,
	.fg = MVPW_WHITE,
};

// widgets
static mvp_widget_t *replaytv_logo;
static mvp_widget_t *rtv_device_menu;
static mvp_widget_t *rtv_browser;
static mvp_widget_t *rtv_episode_description;
static mvp_widget_t *rtv_episode_line[NUM_EPISODE_LINES]; //contained by rtv_episode_description
static mvp_widget_t *rtv_osd_proginfo_widget;     
static mvp_widget_t *rtv_osd_show_title_widget;           //contained by rtv_osd_proginfo_widget
static mvp_widget_t *rtv_osd_show_descr_widget;           //contained by rtv_osd_proginfo_widget
static mvp_widget_t *rtv_popup;                           //show browser popup window
static mvp_widget_t *rtv_jump_to_time_widget;             //alligned to right of gui.c clock_widget
static mvp_widget_t *splash;

static struct rtv_device_descr {
   mvp_widget_t *container;
   mvp_widget_t *name;
   mvp_widget_t *model;
   mvp_widget_t *ipaddr;
   mvp_widget_t *capacity;
   mvp_widget_t *inuse;
   mvp_widget_t *free;
   mvp_widget_t *percentage;
   mvp_widget_t *graph;
} rtv_device_descr;


static void free_ndx_chunk(rtv_ndx_info_t *ndx_info);
static int get_ndx_chunk(unsigned int start_rec, rtv_ndx_info_t *ndx_info);
static int rtv_abort_read(void);
static void* thread_read_start(void *arg);
static void rtv_browser_key_callback(mvp_widget_t *widget, char key);

//************************************************************************************************************************
//******************         local functions             *****************************************************************
//************************************************************************************************************************

// sort_devices()
// called by qsort to sort device list
//
static int sort_devices(const void *item1, const void *item2)
{
   const rtv_device_t *dev1 = *(rtv_device_t**)item1;
   const rtv_device_t *dev2 = *(rtv_device_t**)item2;
   return(strcmp(dev1->device.name, dev2->device.name));
}

// sort_shows()
// called by qsort to sort show list
//
static int sort_shows(const void *item1, const void *item2)
{
   const rtv_show_export_t *sh1 = *(rtv_show_export_t**)item1;
   const rtv_show_export_t *sh2 = *(rtv_show_export_t**)item2;
   int rc;

   if ( (rc = strcmp(sh1->title, sh2->title)) == 0 ) {
      if ( sh1->file_info->time < sh2->file_info->time ) {
         rc = -1;
      }
      else {
         rc = 1;
      }
   }
   return(rc);
}

// get_show_idx()
// Find the index for 'show' in 'guide'
// *show_idx is updated.
// returns 0 for SUCCESS or -1 for FAIL
//
static int get_show_idx( const rtv_guide_export_t  *guide,
                         const rtv_show_export_t   *show,
                         unsigned int              *show_idx )
{
   int x;
   for ( x=0; x < guide->num_rec_shows; x++ ) {
      if ( guide->rec_show_list[x].show_id == show->show_id ) {
         *show_idx = x;
         break;
      }
   }
   if ( x == guide->num_rec_shows ) {
      printf("Error: %s: show not found\n", __FUNCTION__);
      return(-1);
   }
   return(0);
}

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

// breakup_string()
// break up a string into seperate lines no longer than line_width.
// line_ptr returns array of pointers to each line.
//
static int breakup_string(char *str, char **line_ptrs, int num_lines, int font_id, int line_width)
{
   char *cur_start, *cur_end, *str_cpy;
   int   wlen, slen, words, i, ccnt, cwidth, cline, space_width;

   str = strip_spaces(str); // remove leading/trailing spaces from original string
   slen = strlen(str);
   str_cpy = malloc(slen + 2);
   strcpy(str_cpy, str);
   str_cpy[slen]   = ' ';   // need a 1 space at end of copy string
   str_cpy[slen+1] = '\0';

   cur_start = str_cpy;
   words = 0;
   while ( (cur_end = strchr(cur_start, ' ')) != NULL ) {
      *cur_end = '\0';
      cur_start = cur_end+1;
      words++;
   }

   for ( i=0; i < num_lines; i++ ) {
      line_ptrs[i] = &(str[slen]); //empty string
   }

   ccnt=0; cwidth = 0; cline = 0;
   space_width        = mvpw_font_width(font_id, " ");
   cur_start          = str_cpy;
   line_ptrs[cline] = str;
   while ( (ccnt+1) < slen )  {
      wlen = strlen(cur_start);
      if ( (cwidth += mvpw_font_width(font_id, cur_start)) < line_width ) {
         ccnt     += (wlen + 1); 
         cwidth   += space_width;
         cur_start = &(str_cpy[ccnt]);
         //printf("    cs: %s (%d)  %d:%d\n", cur_start, strlen(cur_start), slen, ccnt);
      }
      else {
         str[ccnt-1] = '\0';

         cline++;
         if ( cline == num_lines ) {
            //printf("WARNING: RTV: string overflow: %s\n", &(str[ccnt]));
            break;
         } 
         line_ptrs[cline] = &(str[ccnt]);
         //printf("line %d: (%d:%d)%s\n", cline, slen, ccnt, line_ptrs[cline]);
         cwidth = mvpw_font_width(font_id, cur_start); 
      }
   }

   //for ( i=0; i < num_lines; i++ ) {
   //   printf("l%d: %s\n", i, line_ptrs[i]);
   //}
   free(str_cpy);
   return(0);
}

// calc_string_window_sz()
// return w, h, lines
//
static int calc_string_window_sz(char *str, int font_id, int *width, int *height, int *lines)
{
   int num_lines = 0;
   int max_width = 0;

   char *cur, *next, *s_cpy, *eos, *nl;
   int tmp_width;


   s_cpy = strdup(str);
   eos   = s_cpy + strlen(str);
   cur   = s_cpy;

   while ( cur != eos ) {
      if ( (nl = strchr(cur, '\n')) != NULL ) {
         *nl = '\0';
         next = nl + 1;
      }
      else {
         next = eos;
      }
      
      tmp_width = mvpw_font_width(font_id, cur);
      if ( tmp_width > max_width ) {
         max_width = tmp_width;
      }
      num_lines++;
      cur = next;
      //printf("line: %d   width=%d\n", num_lines, tmp_width); 
   }

   free(s_cpy);
   *width  = max_width;
   *height = num_lines * mvpw_font_height(font_id);
   *lines  = num_lines;
   //printf("BOX: width=%d  height=%d\n", *width, *height); 
   return(0);
}


// rtv_video_queue_write()
// rtv_video_queue_read() 
// rtv_video_queue_flush() 

// Classical producer / consumer implementation of a ping-pong buffer.
// rtv_video_queue_write() is called from get_mpeg_callback() that is in the 
// thread that is reading the streaming rtv file. 
// rtv_video_queue_read() is called from the video_callback_t read_dyn fxn ptr
// in the video.c video_read_thread.
//
static int rtv_video_queue_write(char *buf, int off, int len)
{
	//printf("------------->>> IN %s\n", __FUNCTION__);
   sem_wait(&rtv_video_state.vidq.sem_not_full);
   pthread_mutex_lock(&rtv_video_state.vidq.queue_lock);
  
   rtv_video_state.vidq.buffer[rtv_video_state.vidq.write_pos]   = buf;
   rtv_video_state.vidq.data_off[rtv_video_state.vidq.write_pos] = off;
   rtv_video_state.vidq.data_len[rtv_video_state.vidq.write_pos] = len;
   rtv_video_state.vidq.write_pos++;
   rtv_video_state.vidq.write_pos %= RTV_VID_Q_SZ;

   pthread_mutex_unlock(&rtv_video_state.vidq.queue_lock);
   sem_post(&rtv_video_state.vidq.sem_not_empty);
	return(len);
}

static void rtv_video_queue_flush(void)
{
   int x;

   pthread_mutex_lock(&rtv_video_state.vidq.queue_lock);
   if ( rtv_video_state.vidq.last_read_buf != NULL ) {
      free(rtv_video_state.vidq.last_read_buf);
      rtv_video_state.vidq.last_read_buf = NULL;
   }
   for ( x=0; x < RTV_VID_Q_SZ; x++ ) {
      if ( sem_trywait(&rtv_video_state.vidq.sem_not_empty) != 0 ) {
            break;
      }
      if ( rtv_video_state.vidq.buffer[rtv_video_state.vidq.read_pos] != NULL ) {
         free(rtv_video_state.vidq.buffer[rtv_video_state.vidq.read_pos]);
      }
      sem_post(&rtv_video_state.vidq.sem_not_full);
      rtv_video_state.vidq.read_pos++;
      rtv_video_state.vidq.read_pos %= RTV_VID_Q_SZ;
   }
   pthread_mutex_unlock(&rtv_video_state.vidq.queue_lock);

}

//+*************************************
//   Video Callback functions
//+*************************************

// open callback
//
static int rtv_open(void)
{
	//printf("------------->>> IN %s\n", __FUNCTION__);
	return 0;
}

// seek callback
//
static long long rtv_seek(long long offset, int whence)
{
	//printf("------------->>> IN %s %lld\n", __FUNCTION__, rtv_video_state.pos);
	return (rtv_video_state.pos);
}

// read callback
//
static int rtv_video_queue_read(char **bufp, int len)
{
   int buflen;

   //printf("------------->>> IN %s\n", __FUNCTION__);
   sem_wait(&rtv_video_state.vidq.sem_not_empty);
   pthread_mutex_lock(&rtv_video_state.vidq.queue_lock);

   if ( rtv_video_state.vidq.data_len[rtv_video_state.vidq.read_pos] > len ) {
      printf("***ERROR: %s: data sz(%d) > req sz(%d)\n", __FUNCTION__, 
             rtv_video_state.vidq.data_len[rtv_video_state.vidq.read_pos], len);
      return(-ERANGE);
   }

   if ( rtv_video_state.vidq.last_read_buf != NULL ) {
      free(rtv_video_state.vidq.last_read_buf);
   }
   rtv_video_state.vidq.last_read_buf = rtv_video_state.vidq.buffer[rtv_video_state.vidq.read_pos];
  
   *bufp  = rtv_video_state.vidq.buffer[rtv_video_state.vidq.read_pos] + rtv_video_state.vidq.data_off[rtv_video_state.vidq.read_pos];
   buflen = rtv_video_state.vidq.data_len[rtv_video_state.vidq.read_pos];
   rtv_video_state.vidq.buffer[rtv_video_state.vidq.read_pos] = NULL;
   rtv_video_state.vidq.read_pos++;
   rtv_video_state.vidq.read_pos %= RTV_VID_Q_SZ;

   pthread_mutex_unlock(&rtv_video_state.vidq.queue_lock);
   sem_post(&rtv_video_state.vidq.sem_not_full);
	return (buflen);
}

// file size callback
//
static long long rtv_size(void)
{
	//printf("------------->>> IN %s: %lld\n", __FUNCTION__, rtv_video_state.show_p->file_info->size);
	return(rtv_video_state.show_p->file_info->size);
}

// rtv_notify()
// get mvp core events
//
static void rtv_notify(mvp_notify_t event) 
{
   if ( event == MVP_READ_THREAD_IDLE ) {
      sem_post(&sem_mvp_readthread_idle);
   }
}

static void jumpto_timer_callback(mvp_widget_t *widget)
{
   mvpw_hide(rtv_jump_to_time_widget);
   rtv_video_state.processing_jump_input = 0;
}

// rtv_video_key()
// Special handling of key presses during video playback
//
static int rtv_video_key(char key)
{
   static char jump_str[10];

   int                  rc          = 0;
   int                  jump        = 0;
   int                  jumpto_secs = 0;
   int                  cur_secs, tmp, rec_no, start_rec;
   av_stc_t             stc_time;
   
	switch (key) {
	case MVPW_KEY_ZERO ... MVPW_KEY_NINE:
      if ( !(rtv_video_state.processing_jump_input) ) {
         strcpy(jump_str, " 000");
      }
      jump_str[1] = jump_str[2];
      jump_str[2] = jump_str[3];
      jump_str[3] = key;
      mvpw_set_text_str(rtv_jump_to_time_widget, jump_str);
      if ( rtv_video_state.processing_jump_input ) {
         mvpw_hide(rtv_jump_to_time_widget);
      }
      mvpw_show(rtv_jump_to_time_widget);
      rtv_video_state.processing_jump_input = 1;
      mvpw_set_timer(rtv_jump_to_time_widget, jumpto_timer_callback, 3000);
      rc = 1;
      break;
	case MVPW_KEY_GO:
	case MVPW_KEY_GREEN:      
      //printf("-->%s: handle: jump\n", __FUNCTION__);
      if ( rtv_video_state.processing_jump_input ) {
         strcat(jump_str, " JUMP");
         mvpw_set_text_str(rtv_jump_to_time_widget, jump_str);
         mvpw_hide(rtv_jump_to_time_widget);
         mvpw_show(rtv_jump_to_time_widget);
         mvpw_set_timer(rtv_jump_to_time_widget, jumpto_timer_callback, 3000);

         tmp = atoi(&(jump_str[1]));
         if ( tmp > 99 ) {
            //treat  1st digit as hours
            tmp = ((tmp / 100) * 60) + (tmp % 100);
         }
         jumpto_secs = tmp * 60;
         rtv_video_state.processing_jump_input = 0;
         jump = 1; 
      }
      rc = 1;
      break;
	case MVPW_KEY_REPLAY:
	case MVPW_KEY_REWIND:
	case MVPW_KEY_SKIP:
      av_current_stc(&stc_time);
      cur_secs = (stc_time.hour*60 + stc_time.minute)*60 + stc_time.second;
      if ( cur_secs > rtv_video_state.show_p->duration_sec ) {
         cur_secs = rtv_video_state.show_p->duration_sec;
      }
      printf("-->%s: handle: key='%c'  ct=%d\n", __FUNCTION__, key, cur_secs);
      if      ( key == MVPW_KEY_REPLAY ) { jumpto_secs = cur_secs -  8; }
      else if ( key == MVPW_KEY_REWIND ) { jumpto_secs = cur_secs -  3; }
      else                               { jumpto_secs = cur_secs + 28; }
      jump = 1; 
      rc   = 1;
      break;
	case MVPW_KEY_LEFT:
	case MVPW_KEY_RIGHT:
	case MVPW_KEY_UP:
	case MVPW_KEY_DOWN:
      printf("-->%s: noaction: key='%c'\n", __FUNCTION__, key);
      rc = 1;
		break;
	default:
		break;
	}

   if ( jump == 1 ) {
      int rtn;

      if ( jumpto_secs < 0 ) {
         jumpto_secs = 0;
      }
      if ( jumpto_secs >= rtv_video_state.show_p->duration_sec ) {
         jumpto_secs = rtv_video_state.show_p->duration_sec - 1;
      }
      printf("-->%s: jumpto: %d\n", __FUNCTION__, jumpto_secs);

      if ( jumpto_secs != 0) {
         rec_no = (jumpto_secs * 2) - 1;
      }
      else {
         rec_no = 0;
      }
      if ( (rec_no < rtv_video_state.ndx_info.start_rec_num)                                        || 
           (rec_no >= (rtv_video_state.ndx_info.start_rec_num + rtv_video_state.ndx_info.recs_in_mem)) ) {
         // Need to load a new ndx file block.
         // Make the record we are looking for 25% of the way into the block.
         //
         start_rec = rec_no - (rtv_video_state.ndx_info.rec_cnt_to_load / 4);
         if ( start_rec < 0 ) {
            start_rec = 0;
         }
         else if ( start_rec > (rtv_video_state.ndx_info.num_rec - (rtv_video_state.ndx_info.rec_cnt_to_load * 3/4)) ) {
            start_rec = rtv_video_state.ndx_info.num_rec - rtv_video_state.ndx_info.rec_cnt_to_load; 
         }
         if ( start_rec < 0 ) {
            start_rec = 0;
         }
         if ( (rtn = get_ndx_chunk(start_rec, &rtv_video_state.ndx_info)) ) {
            printf("***ERROR: RTV failed to read ndx file chunk: rc=%d\n", rtn);
            return(1);
         }
      }

      // Double check to make sure we got the record in memory
      //
      if ( rtv_video_state.ndx_info.file_chunk.buf == NULL ) {
         printf("***ERROR: RTV SW BUG Unexpected NULL ndx chunk pointer\n");
         return(1);
      }
      if ( (rec_no < rtv_video_state.ndx_info.start_rec_num)                                        || 
           (rec_no >= (rtv_video_state.ndx_info.start_rec_num + rtv_video_state.ndx_info.recs_in_mem)) ) {
         printf("***ERROR: RTV SW BUG ndx chunk is screwed up\n");
         return(1);
      }

      // Jump to the new spot
      // 
      if ( rtv_video_state.ndx_info.ver == RTV_NDX_30 ) {
         rtv_ndx_30_record_t *ndx_recs = (rtv_ndx_30_record_t*)rtv_video_state.ndx_info.file_chunk.data_start;
         rtv_ndx_30_record_t  rec;

         memcpy(&rec, &(ndx_recs[rec_no - rtv_video_state.ndx_info.start_rec_num]), sizeof(rtv_ndx_30_record_t));
         rtv_convert_30_ndx_rec(&rec);

         if ( jumpto_secs != 0) {
            rtv_video_state.pos = rec.filepos_iframe;
         } 
         else {
            rtv_video_state.pos = 0;
         }
         rtv_print_30_ndx_rec("JumpTo      ", rec_no, &rec);
         
         // Stop streaming
         //
         video_stop_play();
         rtv_abort_read();
         video_clear();

         // Restart stream
         //
         av_play();
         demux_reset(handle);         
         if ( rtv_video_state.pos != 0 ) {
            rtv_video_state.chunk_offset = rtv_video_state.pos % 0x7FFF; //offset withing 32K chunk
         }
         pthread_create(&rtv_stream_read_thread, NULL, thread_read_start, (void*)rtv_video_state.show_p->file_name);
         rtv_video_state.play_state = RTV_VID_PLAYING;
         video_play(NULL); // kick video.c 
      } 

   } //jumping
   
   return(rc);
}

//+*************************************
//   file processing functions
//+*************************************

// rtv_abort_read()
// abort rtv_read_file_chunked() stream
//
static int rtv_abort_read(void)
{
   if ( (rtv_video_state.play_state != RTV_VID_STOPPED)    &&
        (rtv_video_state.play_state != RTV_VID_ABORT_PLAY)  ) {

      // Set state to ABORT_PLAY so rtv_stream_read_thread will exit.
      // Flush the video queue incase rtv_stream_read_thread is 
      // blocked trying to write to a full queue.
      // Write a null buffer to the video queue incase the mvp read thread is 
      // blocked waiting on an empty video queue.
      //
      sem_init(&sem_mvp_readthread_idle, 0, 0);
      rtv_video_state.play_state = RTV_VID_ABORT_PLAY;
      rtv_video_queue_flush();
      pthread_join(rtv_stream_read_thread, NULL);
      rtv_video_queue_flush();
      rtv_video_queue_write(NULL, 0, -1);

      sem_wait(&sem_mvp_readthread_idle); // wait for the mvp read thread to idle
      rtv_video_state.play_state = RTV_VID_STOPPED;
   }
   return(0);
}

// get_mpeg_callback()
// rtvlib callback for rtv_read_file_chunked()
//
static int get_mpeg_callback(unsigned char *buf, size_t len, size_t offset, void *vd)
{
   if ( rtv_video_state.play_state == RTV_VID_ABORT_PLAY ) {
      free(buf);
      return(1);
   }
   if ( rtv_video_state.chunk_offset ) {
      // We just did a jump. Add in the offset to get to start
      // of a GOP.
      //
      offset += rtv_video_state.chunk_offset;
      len    -= rtv_video_state.chunk_offset;
      rtv_video_state.chunk_offset = 0;
   } 
   rtv_video_queue_write(buf, offset, len);
   rtv_video_state.pos += len;
   if ( rtv_video_state.play_state == RTV_VID_ABORT_PLAY ) {
      return(1);
   }
   return(0);
}


// get_mpeg_file()
// start a rtv mpeg file read stream
//
static void get_mpeg_file(rtv_device_t *rtv, char *filename, __u64 pos, int ToStdOut) 
{
   char pathname[MAX_FILENAME_LEN];
   int i;
   
   if ( strlen(filename) + strlen("/Video/") + 1 > sizeof(pathname) ) {
      fprintf(stderr, "mpeg filename too long\n");
      exit(-1);
   }
   
   sprintf(pathname, "/Video/%s", filename);
   
   //Tell the user we're up to something
   fprintf(stderr, "Retrieving /Video/%s...\n", filename); 
   
   //Send the request for the file
   i = rtv_read_file_chunked( &(rtv->device), pathname, pos, 0, 0, get_mpeg_callback, NULL );
   
   //all done, cleanup as we leave 
   fprintf(stderr, "\nDone.\n"); 
} 


// thread_read_start()
// setup rtv mpeg file read stream
//
static void* thread_read_start(void *arg)
{
   int x;

   printf("rtv stream read thread is pid %d\n", getpid());
   
   // Init the video structure & queue
   //
   rtv_video_state.vidq.write_pos     = 0;
   rtv_video_state.vidq.read_pos      = 0;
   rtv_video_state.vidq.last_read_buf = NULL;

   for ( x=0; x < RTV_VID_Q_SZ; x++ ) {
      rtv_video_state.vidq.buffer[x]   = NULL;
      rtv_video_state.vidq.data_off[x] = 0;
      rtv_video_state.vidq.data_len[x] = 0;
   }

   pthread_mutex_init(&rtv_video_state.vidq.queue_lock, NULL);
   sem_init(&rtv_video_state.vidq.sem_not_empty, 0, 0);
   sem_init(&rtv_video_state.vidq.sem_not_full, 0, RTV_VID_Q_SZ);

   // Stream the file
   //
   get_mpeg_file(current_rtv_device, (char*)arg, rtv_video_state.pos & ~(0x7FFFull), 0);
   pthread_exit(NULL);
   return NULL;
}

static void free_ndx_chunk(rtv_ndx_info_t *ndx_info)
{
   if ( ndx_info->file_chunk.buf != NULL ) {
      free(ndx_info->file_chunk.buf);
      ndx_info->file_chunk.buf        = NULL;
      ndx_info->file_chunk.data_start = NULL;
      ndx_info->file_chunk.len        = 0;
   }
}

// get_ndx_chunk()
// Get a block of ndx records.
//
static int get_ndx_chunk(unsigned int start_rec, rtv_ndx_info_t *ndx_info)
{
   unsigned int         rd_sz, rd_pos;
   int                  rc;
   rtv_device_info_t   *devinfo;
   rtv_ndx_30_record_t *ndx_recs;
   rtv_ndx_30_record_t  rec;

   devinfo = (rtv_device_info_t*)&(current_rtv_device->device); //cast to override volatile warning
   free_ndx_chunk(ndx_info);

   if ( (ndx_info->ver == RTV_NDX_22) || (ndx_info->ver == RTV_NDX_30) ) {
      rd_pos = (start_rec * ndx_info->rec_sz) + ndx_info->hdr_sz;
      if ( rd_pos > (ndx_info->file_sz - ndx_info->rec_sz ) ) {
         printf("***ERROR: %s: bad start rec: %u\n", __FUNCTION__, start_rec);
         return(-ESPIPE); //off the end of the file
      }
      rd_sz  = ndx_info->rec_sz * ndx_info->rec_cnt_to_load;
      if ( (rd_pos + rd_sz) > ndx_info->file_sz ) {
         rd_sz = ndx_info->file_sz - rd_pos; //truncate read to not go past EOF
      }

      rc = rtv_read_file(devinfo, ndx_info->filename, rd_pos, rd_sz, &(ndx_info->file_chunk));
      if ( rc != 0 ) {
         return(rc);
      }

      ndx_info->start_rec_num = start_rec;
      ndx_info->recs_in_mem   = rd_sz /ndx_info->rec_sz;
      ndx_recs = (rtv_ndx_30_record_t*)ndx_info->file_chunk.data_start;
      printf("NDX: chunk loaded: rec=%u pos=%u sz=%u\n", start_rec, rd_pos, rd_sz);
      memcpy(&rec, ndx_recs, sizeof(rtv_ndx_30_record_t));
      rtv_convert_30_ndx_rec(&rec);
      rtv_print_30_ndx_rec("LoadStartRec", ndx_info->start_rec_num, &rec);
      memcpy(&rec, &(ndx_recs[ndx_info->recs_in_mem-1]), sizeof(rtv_ndx_30_record_t));
      rtv_convert_30_ndx_rec(&rec);
      rtv_print_30_ndx_rec("LoadEndRec  ", ndx_info->start_rec_num + ndx_info->recs_in_mem - 1, &rec);
   }

   return(0);
}

// play_show()
//
static void play_show(const rtv_show_export_t *show, int start_gop)
{
   rtv_ndx_info_t      *ndx_info = &(rtv_video_state.ndx_info); 
   char                 show_base_name[MAX_FILENAME_LEN];
   rtv_device_info_t   *devinfo;
   rtv_fs_file_t        fileinfo;
   int                  len, rc;
   unsigned int         first_ndx_rec;

   devinfo = (rtv_device_info_t*)&(current_rtv_device->device); //cast to override volatile warning

   mvpw_hide(rtv_browser);
   mvpw_hide(replaytv_logo);
   mvpw_hide(rtv_episode_description);
   av_move(0, 0, 0);
   screensaver_disable();
   mvpw_show(root);
   mvpw_expose(root);
   mvpw_focus(root);
   
   printf("\n"); rtv_print_show(show, 0); printf("\n");
   if ( (len = strlen(show->file_name)) > (MAX_FILENAME_LEN-8) ) {
      printf("***ERROR: show file name too long: %d\n", len);
      return;
   } 

   // Get .ndx & .evt info
   //
   free_ndx_chunk(ndx_info);
   memset(&(rtv_video_state.evt_info), 0, sizeof(rtv_video_state.evt_info));

   memcpy(show_base_name, show->file_name, len-4);
   show_base_name[len-4] = '\0'; 
   sprintf(ndx_info->filename, "/Video/%s.ndx", show_base_name);
   printf("NDX: %s\n", ndx_info->filename);
   rc = rtv_get_file_info(devinfo, ndx_info->filename, &fileinfo );
   if ( rc == 0 ) {
      rtv_http_resp_data_t file_data;
      //rtv_print_file_info(&fileinfo);
      if ( fileinfo.size > sizeof(rtv_ndx_30_header_t) ) {
         rc = rtv_read_file(devinfo, ndx_info->filename, 0, sizeof(rtv_ndx_30_header_t), &file_data);
         if ( rc == 0 ) {
            //rtv_hex_dump("NDX HDR", buf_p, sizeof(rtv_ndx_30_header_t));
            if ( (file_data.data_start[0] == 3) && (file_data.data_start[1] == 0) ) {
               ndx_info->ver    = RTV_NDX_30;
               ndx_info->hdr_sz = sizeof(rtv_ndx_30_header_t);
               ndx_info->rec_sz = sizeof(rtv_ndx_30_record_t);
               ndx_info->rec_cnt_to_load = 600;
               if ( ((fileinfo.size - sizeof(rtv_ndx_30_header_t)) % sizeof(rtv_ndx_30_record_t)) != 0 ) {
                  printf("\n***WARNING: ndx file size not consistant with record size\n\n");
               }
               ndx_info->num_rec = (fileinfo.size - sizeof(rtv_ndx_30_header_t)) / sizeof(rtv_ndx_30_record_t);
            }
            else if ( (file_data.data_start[0] == 2) && (file_data.data_start[2] == 0) ) {
               ndx_info->ver    = RTV_NDX_22;
               ndx_info->hdr_sz = sizeof(rtv_ndx_22_header_t);
               ndx_info->rec_sz = sizeof(rtv_ndx_22_record_t);
               ndx_info->rec_cnt_to_load = 600;
               if ( ((fileinfo.size - sizeof(rtv_ndx_22_header_t)) % sizeof(rtv_ndx_22_record_t)) != 0 ) {
                  printf("\n***WARNING: ndx file size not consistant with record size\n\n");
               }
               ndx_info->num_rec = (fileinfo.size - sizeof(rtv_ndx_22_header_t)) / sizeof(rtv_ndx_22_record_t);
               ndx_info->com_skip_ok = 1;
            }
            else {
               printf("***ERROR: invalid ndx file version: %d %d\n", file_data.data_start[0], file_data.data_start[1]);
            }
            free(file_data.buf);
         }
      }
      ndx_info->file_sz = fileinfo.size;
      printf("NDX: ver=%d size=%u rec=%u\n", ndx_info->ver, ndx_info->file_sz, ndx_info->num_rec);
      rtv_free_file_info(&fileinfo);
   }

   if ( ndx_info->ver == RTV_NDX_30 ) {
      // Setup the .evt struct.
      //
      sprintf(rtv_video_state.evt_info.filename, "/Video/%s.evt", show_base_name);
      printf("EVT: %s\n", rtv_video_state.evt_info.filename);
      rc = rtv_get_file_info(devinfo, rtv_video_state.evt_info.filename, &fileinfo );
      if ( rc == 0 ) {
         rtv_video_state.evt_info.hdr_sz = RTV_EVT_HDR_SZ;
         if ( ((fileinfo.size - RTV_EVT_HDR_SZ) % sizeof(rtv_evt_record_t)) != 0 ) {
            printf("\n***WARNING: evt file size not consistant with record size\n\n");
         }
         rtv_video_state.evt_info.num_rec = (fileinfo.size - RTV_EVT_HDR_SZ) / sizeof(rtv_evt_record_t);
         rtv_video_state.evt_info.file_sz = fileinfo.size;
         printf("EVT: size=%u rec=%u\n\n", rtv_video_state.evt_info.file_sz, rtv_video_state.evt_info.num_rec);
         rtv_free_file_info(&fileinfo);
      }
   }

   // Each GOP is 1/2 second. (1 ndx rec)
   // Load the ndx record for GOP - 10sec. = GOP - 20
   //
   if ( (start_gop - 20) < 0 ) {
      first_ndx_rec = 0; 
   }
   else {
      first_ndx_rec = start_gop - 20;
   }
   get_ndx_chunk(first_ndx_rec, ndx_info);

   printf("Playing file: %s\n", show->file_name);  
   if ( rtv_video_state.ndx_info.ver == RTV_NDX_30 ) {
      rtv_ndx_30_record_t *ndx_recs = (rtv_ndx_30_record_t*)rtv_video_state.ndx_info.file_chunk.data_start;
      rtv_ndx_30_record_t  rec;
      
      memcpy(&rec, &(ndx_recs[start_gop - rtv_video_state.ndx_info.start_rec_num]), sizeof(rtv_ndx_30_record_t));
      rtv_convert_30_ndx_rec(&rec);
      
      if ( start_gop != 0) {
         rtv_video_state.pos = rec.filepos_iframe;
         rtv_video_state.chunk_offset = rtv_video_state.pos % 0x7FFF; //offset within 32K chunk
      } 
      else {
         rtv_video_state.pos          = 0;
         rtv_video_state.chunk_offset = 0;
      }
      rtv_print_30_ndx_rec("StartGOP      ", start_gop, &rec);   
   }
   else {
      rtv_video_state.pos          = 0;
      rtv_video_state.chunk_offset = 0;
   }

   // Play the show
   //
   printf("Playing file: %s\n", show->file_name);  
   av_play();
   demux_reset(handle);

   pthread_create(&rtv_stream_read_thread, NULL, thread_read_start, (void*)show->file_name);
   rtv_video_state.show_p                = show;
   rtv_video_state.play_state            = RTV_VID_PLAYING;
   rtv_video_state.processing_jump_input = 0;
   video_play(NULL); // kick video.c 
}


//+*************************************
//   GUI stuff
//+*************************************

// msg_win_destroy_any_key_callback()
// Destroy for any keypress
static void msg_win_destroy_any_key_callback(mvp_widget_t *widget, char key)
{
   mvpw_destroy(widget);
}

// show_message_window()
//
static mvp_widget_t* show_message_window(void (*callback)(mvp_widget_t *widget, char key), char *message)
{
   int x, y, w, h, lines;
   mvp_widget_t *wp;

   rtv_message_window_attr.font = fontid;  
   calc_string_window_sz(message, rtv_message_window_attr.font, &w, &h, &lines);
   w += (rtv_message_window_attr.margin * 2);
   h += (rtv_message_window_attr.margin * lines);

   x = (scr_info.cols - w) / 2;
   y = (scr_info.rows - h) / 2;

   wp = mvpw_create_text(NULL, x, y, w, h, MVPW_BLACK, MVPW_GREEN, 4);
   mvpw_set_key(wp, callback);
   mvpw_set_text_attr(wp, &rtv_message_window_attr);
   mvpw_set_text_str(wp, message);
   mvpw_raise(wp);
   mvpw_show(wp);
   mvpw_focus(wp);
   mvpw_expose(wp);
	mvpw_event_flush();
   return(wp);
}

// rtv_guide_hilite_callback()
//
static void rtv_guide_hilite_callback(mvp_widget_t *widget, char *item, void *key, int hilite)
{
   rtv_show_export_t   *show;
   mvpw_widget_info_t   winfo;
   char                 strp[1024];
   char                *pos;
   char                *line_ary[NUM_EPISODE_LINES-2];
   int                  x;

	if (hilite) {
      pos  = strp;
      show = (rtv_show_export_t*)key;
      rtv_video_state.show_p = show;  //update the show pointer in the global struct.

      mvpw_get_widget_info(rtv_episode_description, &winfo);      
      mvpw_set_text_str(rtv_episode_line[0], show->title);
      pos += sprintf(strp, "%s; recorded %s from %d (%s)", show->duration_str, show->file_info->time_str_fmt2, show->tuning, show->tune_chan_name);
      mvpw_set_text_str(rtv_episode_line[1], strp);

      // Do show rating, movie stars, movie year
      //
      pos = strp;
      pos[0] = '\0';
      if ( show->flags.movie ) {
         // Movie
         *pos++ = '(';
         if ( show->movie_stars ) {
            memset(pos, '*', show->movie_stars);
            pos += show->movie_stars;
            *pos++ = ',';
            *pos++ = ' ';
         }
         pos += sprintf(pos, "%s, %d) ", show->rating, show->movie_year);
      }
      else {
         // TV show
         if ( strcmp(show->rating, "") != 0 ) {
            pos += sprintf(pos, "(%s) ", show->rating);
         }
         if ( strcmp(show->episode, "") != 0 ) {
            pos += sprintf(pos, "%s", show->episode);
            if ( strcmp(show->description, "") != 0 ) {
               pos += sprintf(pos, ": ");
            }
         }        
      }

      // do description, actors, ...
      //
      if ( strcmp(show->description, "") != 0 ) {
         pos += sprintf(pos, "%s ", show->description);
      }
      if ( strcmp(show->actors, "") != 0 ) {
         pos += sprintf(pos, "%s", show->actors);
         if ( strcmp(show->guest, "") != 0 ) {
            pos += sprintf(pos, ", ");
         }
      }
      if ( strcmp(show->guest, "") != 0 ) {
         pos += sprintf(pos, "%s", show->guest);
      }
     
      // display it
      //
      breakup_string(strp, line_ary, NUM_EPISODE_LINES-2, rtv_episode_descr_attr.font, winfo.w);
      for ( x=0; x < NUM_EPISODE_LINES-2; x++ ) {
         mvpw_set_text_str(rtv_episode_line[x+2], line_ary[x]);
      }
      mvpw_hide(rtv_episode_description);
      mvpw_show(rtv_episode_description);
	} else {
	}
}

// rtv_guide_select_callback()
//
static void rtv_guide_select_callback(mvp_widget_t *widget, char *item, void *key)
{
   // do nothing
   // handled by rtv_browser_key_callback() 
}

// rtv_get_guide
//
static int rtv_get_guide(mvp_widget_t *widget, rtv_device_t *rtv)
{
   rtv_fs_volume_t     *volinfo;
   rtv_guide_export_t  *guide;
   rtv_show_export_t  **sorted_show_ptr_list;
   int                  x, rc;
   
   // Verify we can access the Video directory. If not the RTV's time is probably off by more
   // than 40 seconds from ours.
   rc = rtv_get_volinfo( &(rtv->device), "/Video", &volinfo );
   if ( rc != 0 ) {
      fprintf(stderr, "**ERROR: Failed to access /Video directory for RTV %s\n", rtv->device.name);
      fprintf(stderr, "         The RTV's clock and this clock must be within 40 seconds of each other\n");
      show_message_window(msg_win_destroy_any_key_callback, 
                          "ERROR: ReplayTV /Video dir access failed.\n"
                          "MVP & ReplayTV clocks must not differ by\n"
                          "more than 40 seconds.\n"
                          "Press any key to continue");
      mvpw_event_flush();
      return(-1);
   }
   rtv_free_volinfo(&volinfo);
   
   guide = &(rtv->guide);
   if ( (rc = rtv_get_guide_snapshot( &(rtv->device), NULL, guide)) != 0 ) {
      fprintf(stderr, "**ERROR: Failed to get Show Guilde for RTV %s\n", rtv->device.name);
      show_message_window(msg_win_destroy_any_key_callback, 
                          "ERROR: Failed to get guide snapshot.\n"
                          "Press any key to continue");
      mvpw_event_flush();
      return(-1);
   }

   // Make a sorted list & build menu
   //
   sorted_show_ptr_list = malloc(sizeof(sorted_show_ptr_list) * guide->num_rec_shows);
   for ( x=0; x < guide->num_rec_shows; x++ ) {
      sorted_show_ptr_list[x] = &(guide->rec_show_list[x]);
   }
   qsort(sorted_show_ptr_list, guide->num_rec_shows, sizeof(sorted_show_ptr_list), sort_shows);

   rtv_default_item_attr.select = rtv_guide_select_callback;
   rtv_default_item_attr.hilite = rtv_guide_hilite_callback;

   for ( x=0; x < guide->num_rec_shows; x++ ) {
      char title_episode[255];
      snprintf(title_episode, 254, "%s: %s", sorted_show_ptr_list[x]->title, sorted_show_ptr_list[x]->episode);
      printf("%d:  %s\n", x, title_episode);
      mvpw_add_menu_item(widget, title_episode, sorted_show_ptr_list[x], &rtv_default_item_attr);
   }
   
   //all done, cleanup as we leave 
   //
   fprintf(stderr, "\n[End of Show:Episode Listing.]\n");
   free(sorted_show_ptr_list);
   return(0);
}

// rtv_update_show_browser()
//
static void rtv_update_show_browser(rtv_device_t *rtv)
{
   char          buf[256];
   
   mvpw_clear_menu(rtv_browser);
   snprintf(buf, sizeof(buf), " %s", rtv->device.name);
   mvpw_set_menu_title(rtv_browser, buf);
   
   current_rtv_device = rtv;   
   if ( (rtv_get_guide(rtv_browser, rtv)) != 0 ) {
      return;
   }

   // Bind in the video callback functions
   //
   video_functions = &replaytv_functions;

   mvpw_show(rtv_browser);
   mvpw_show(rtv_episode_description);
   mvpw_focus(rtv_browser);
   rtv_level = RTV_BROWSER_MENU;
   return;
}

// rtv_device_hilite_callback()
//
static void rtv_device_hilite_callback(mvp_widget_t *widget, char *item, void *key, int hilite)
{
   rtv_device_t    *rtv = (rtv_device_t*)key;  
   char             strp[128];
   rtv_fs_volume_t  *volinfo;
   int              percentage;
   int              rc;
   double           size_gig, used_gig, free_gig;



	if (hilite) {

      // Pull volume info for the Video directory. If we get an error the RTV's time is probably off by more
      // than 40 seconds from ours.
      //
      rc = rtv_get_volinfo( &(rtv->device), "/Video", &volinfo );
      if ( rc != 0 ) {
         fprintf(stderr, "**ERROR: Failed to access /Video directory for RTV %s\n", rtv->device.name);
         fprintf(stderr, "         The RTV's clock and this clock must be within 40 seconds of each other\n");
         show_message_window(msg_win_destroy_any_key_callback, 
                             "ERROR: ReplayTV /Video dir access failed.\n"
                             "MVP & ReplayTV clocks must not differ by\n"
                             "more than 40 seconds.\n"
                             "Press any key to continue");
         mvpw_event_flush();
         volinfo = malloc(sizeof(rtv_fs_volume_t));
         memset(volinfo, 0,sizeof(rtv_fs_volume_t)); 
      }

      sprintf(strp, "Name:  %s", rtv->device.name);
      mvpw_set_text_str(rtv_device_descr.name, strp);
      
      if ( atoi(rtv->device.modelNumber) == 4999 ) {
         sprintf(strp, "Model: DVArchive");
      }
      else {
         sprintf(strp, "Model: %s", rtv->device.modelNumber);
      }
      mvpw_set_text_str(rtv_device_descr.model, strp);
      
      sprintf(strp, "IPAddress: %s", rtv->device.ipaddr);
      mvpw_set_text_str(rtv_device_descr.ipaddr, strp);

      if ( volinfo->size == 0 ) {
         //
         // Probably DVArchive. It sends zero for the volume size.
         //
         sprintf(strp, "Capacity: Unknown");
         mvpw_set_text_str(rtv_device_descr.capacity, strp);
         sprintf(strp, " ");
         mvpw_set_text_str(rtv_device_descr.inuse, strp);
         mvpw_set_text_str(rtv_device_descr.free, strp);
         mvpw_set_text_str(rtv_device_descr.percentage, strp);
         mvpw_set_graph_current(rtv_device_descr.graph, 0);
      }
      else {
         size_gig   = (double)volinfo->size / (double)(1000 * 1000 * 1000);
         used_gig   = (double)volinfo->used / (double)(1000 * 1000 * 1000);
         free_gig   = size_gig - used_gig;
         percentage = (int)((double)(volinfo->used_k) / (double)(volinfo->size_k) * 100.0);
         
         
         sprintf(strp, "Capacity: %3.1f GB", size_gig);
         mvpw_set_text_str(rtv_device_descr.capacity, strp);
         sprintf(strp, "Used: %3.1f GB", used_gig);
         mvpw_set_text_str(rtv_device_descr.inuse, strp);
         sprintf(strp, "Free: %3.1f GB", free_gig);
         mvpw_set_text_str(rtv_device_descr.free, strp);
         sprintf(strp, "%d%%", percentage);
         mvpw_set_text_str(rtv_device_descr.percentage, strp);
         mvpw_set_graph_current(rtv_device_descr.graph, percentage);
      }

      rtv_free_volinfo(&volinfo);

      // display it
      //
      mvpw_hide(rtv_device_descr.container);
      mvpw_show(rtv_device_descr.container);
	} else {
	}
}

//  rtv_device_select_callback()
//
static void rtv_device_select_callback(mvp_widget_t *widget, char *item, void *key)
{
   rtv_device_t *rtv = (rtv_device_t*)key;  
   
   // We have to do SSDP (discovery) on DVArchive to put it into RTV 4K/5K mode
   //
   if ( (atoi(rtv->device.modelNumber) == 4999) && (rtvs_discovered == 0) ) {
      printf("\n\n*****\n");
      printf("DVArchive server IP address can not be statically configured.\n");
      printf("Use mvpmc \"-R discover\" option\n");
      printf("*****\n\n");
      return;
   }
   
   mvpw_hide(rtv_device_menu);
   mvpw_hide(rtv_device_descr.container);
   rtv_update_show_browser(rtv);
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
   if (key == MVPW_KEY_EXIT) {
      running_replaytv = 0;
      replaytv_back_to_mvp_main_menu();
   }
}



// called when back/exit pressed from show browser
//
static int rtv_back_to_device_menu(mvp_widget_t *widget)
{
   mvpw_hide(rtv_browser);
   mvpw_hide(rtv_episode_description);
   mvpw_show(rtv_device_menu);
   mvpw_show(rtv_device_descr.container);
   mvpw_focus(rtv_device_menu);
   rtv_level = RTV_DEVICE_MENU;
   return(0);
}


// sub_window_key_callback()
// hide for MENU or EXIT key presses.
static void sub_window_key_callback(mvp_widget_t *widget, char key)
{
	if (key == MVPW_KEY_MENU) {
		mvpw_hide(widget);
	}
	else if (key == MVPW_KEY_EXIT) {
		mvpw_hide(widget);
	}
}

// msg_win_delete_callback()
// 
static void msg_win_delete_callback(mvp_widget_t *widget, char key)
{
   mvp_widget_t *wp = NULL;;
   int           rc;
   unsigned int  show_idx;

   // hide the "delete confirmation" window
   //
   mvpw_hide(widget);
	mvpw_event_flush();

   if (key != MVPW_KEY_OK) {
      printf("CANCEL: delete show\n");
      goto err_exit;
   }

   printf("OK: delete show\n");
   if ( get_show_idx((rtv_guide_export_t*)&(current_rtv_device->guide), rtv_video_state.show_p, &show_idx) != 0 ) {
      goto err_exit;
   }
   
   wp = show_message_window(NULL, "Deleting Show.  \nPlease Wait...  \n");
   mvpw_event_flush();

   // delete the show
   //
   rc = rtv_delete_show((rtv_device_info_t*)&(current_rtv_device->device), 
                        (rtv_guide_export_t*)&(current_rtv_device->guide), 
                        show_idx);   
   if ( rc != 0 ) {
      printf("Error: rtv_delete_show call failed.\n");
      show_message_window(msg_win_destroy_any_key_callback, "Error: rtv_delete_show call failed.\nPress any key to continue");
      mvpw_event_flush();
      goto err_exit;
   }

   rc = rtv_release_show_and_wait((rtv_device_info_t*)&(current_rtv_device->device), 
                                  (rtv_guide_export_t*)&(current_rtv_device->guide), 
                                  show_idx);   
   if ( rc != 0 ) {
      printf("Error: rtv_release_show_and_wait call failed.\n");
   }

   // Update the show browser
   //
   rtv_update_show_browser((rtv_device_t*)current_rtv_device); // cast to override volatile warning
   
err_exit:
   if ( wp != NULL ) {
      mvpw_destroy(wp);
   }
   mvpw_destroy(widget); //we're in this guys callback. So destroying him must be the last thing we do.
   return;
}

// rtv_popup_select_callback()
//
static void rtv_popup_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	int          which = (int)key;
   int          rc, play_gop, in_use;
   unsigned int show_idx;
   char         delete_msg[512];

	switch (which) {

	case RTV_POPUP_PLAY:
		printf("Popup Play\n");

      //cast rtv_guide_export_t & rtv_device_info_t to override volatile warnings
      //
      if ( get_show_idx((rtv_guide_export_t*)&(current_rtv_device->guide), rtv_video_state.show_p, &show_idx) != 0 ) {
         return;
      }
      printf("show idx: %u\n", show_idx);
      
      rc = rtv_get_play_position((rtv_device_info_t*)&(current_rtv_device->device), 
                                 (rtv_guide_export_t*)&(current_rtv_device->guide), 
                                 show_idx, 
                                 &play_gop);
      if ( rc != 0 ) {
         printf("Error: rtv_get_play_position call failed.\n");
         play_gop = 0;
      }
      printf("CurrentGopPosition=0x%x (%u)\n", play_gop, play_gop);
      
      // Jump back 5 seconds from the current GOP.
      //
      if ( (play_gop - 10) < 0 ) {
         play_gop = 0;
      }
      else {
         play_gop -= 10;
      }
      mvpw_hide(rtv_popup);
      play_show(rtv_video_state.show_p, play_gop);
		break;

	case RTV_POPUP_PLAY_FB:
		printf("Popup Play From Beginning\n");
      mvpw_hide(rtv_popup);
      play_show(rtv_video_state.show_p, 0);
		break;

	case RTV_POPUP_DELETE:
		printf("Popup Delete\n");
      if ( get_show_idx((rtv_guide_export_t*)&(current_rtv_device->guide), rtv_video_state.show_p, &show_idx) != 0 ) {
         return;
      }
      printf("show idx: %u\n", show_idx);

      rc = rtv_is_show_inuse((rtv_device_info_t*)&(current_rtv_device->device), 
                             (rtv_guide_export_t*)&(current_rtv_device->guide), 
                             show_idx, 
                             &in_use);      
      mvpw_hide(rtv_popup);
      if ( rc != 0 ) {
         printf("Error: rtv_is_show_inuse call failed.");
         return;
      }      
      if ( in_use ) {         
         show_message_window(msg_win_destroy_any_key_callback, "Delete Canceled\nShow is in use\nPress any key to continue");
         return;
      }
      sprintf(delete_msg, "Delete Show:\n%s\n%s\nRecorded: %s\nPress <OK> to confirm\nPress any other key to cancel", 
              rtv_video_state.show_p->title,
              rtv_video_state.show_p->episode,
              rtv_video_state.show_p->file_info->time_str_fmt2);
      show_message_window(msg_win_delete_callback, delete_msg);
		break;

	case RTV_POPUP_CANCEL:
		mvpw_hide(rtv_popup);
		break;
	} //switch
}

// Bound as rtv_browser widget callback
//
static void rtv_browser_key_callback(mvp_widget_t *widget, char key)
{
 
   //printf("JBH: key===%c\n", key);
	switch (key) {
	case MVPW_KEY_EXIT:
      // Back/Exit
      //
      rtv_back_to_device_menu(widget);
      break;
   case MVPW_KEY_MENU:
   case MVPW_KEY_OK:
      // Menu button
      //
      printf("rtv show browser popup menu\n");
      mvpw_clear_menu(rtv_popup);
      rtv_popup_item_attr.select = rtv_popup_select_callback;
      mvpw_add_menu_item(rtv_popup, "Play",                (void*)RTV_POPUP_PLAY, &rtv_popup_item_attr);
      mvpw_add_menu_item(rtv_popup, "Play from Beginning", (void*)RTV_POPUP_PLAY_FB, &rtv_popup_item_attr);
      mvpw_add_menu_item(rtv_popup, "Delete",              (void*)RTV_POPUP_DELETE, &rtv_popup_item_attr);
      mvpw_add_menu_item(rtv_popup, "Cancel",              (void*)RTV_POPUP_CANCEL, &rtv_popup_item_attr);
      mvpw_show(rtv_popup);
      mvpw_focus(rtv_popup);
      break;
   case MVPW_KEY_PLAY:
      play_show(rtv_video_state.show_p, 0);
      break;
   default:
      break;
   } //switch
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
   rtv_set_32k_chunks_to_merge(VIDEO_BUFF_SIZE / RTV_XFER_CHUNK_SZ);

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

   // Init the video state structure
   //
   rtv_video_state.play_state = RTV_VID_STOPPED;
   rtv_video_state.show_p     = NULL;

   sem_init(&sem_mvp_readthread_idle, 0, 0); // posted by rtv_notify callback

   rtv_initialized = 1;
   return(0);
}


// discover devices and update device menu.
//
int replaytv_device_update(void)
{
   mvp_widget_t      *widget = rtv_device_menu;
   int                h, w, x, y, idx, rc;
   char               buf[128];
   rtv_device_list_t *rtv_list;
   rtv_device_t      **sorted_device_ptr_list;   
   running_replaytv = 1;

   add_osd_widget(rtv_osd_proginfo_widget, OSD_PROGRAM, 1, NULL);
   screensaver_enable();
   
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
   
   // Make a sorted list
   //
   sorted_device_ptr_list = malloc(sizeof(sorted_device_ptr_list) * rtv_devices.num_rtvs);
   for ( idx=0; idx < rtv_devices.num_rtvs; idx++ ) {
      sorted_device_ptr_list[idx] = &(rtv_devices.rtv[idx]);
    }
   qsort(sorted_device_ptr_list, rtv_devices.num_rtvs, sizeof(sorted_device_ptr_list), sort_devices);
   
   // Add discovered devices to the list
   //
   device_menu_item_attr.select = rtv_device_select_callback;
   device_menu_item_attr.hilite = rtv_device_hilite_callback;
   for ( idx=0; idx < rtv_devices.num_rtvs; idx++ ) {
      if ( sorted_device_ptr_list[idx]->device.name != NULL ) {
         if ( (atoi(sorted_device_ptr_list[idx]->device.modelNumber) == 4999) && (rtvs_discovered == 0) ) {
            // DVarchive must be discovered to kick it into 5K mode
            //
            char name[80];
            snprintf(name, 80, "%s:(Unavailable)", sorted_device_ptr_list[idx]->device.name);
            mvpw_add_menu_item(widget, name, sorted_device_ptr_list[idx], &device_menu_item_attr);
         }
         else {
            mvpw_add_menu_item(widget, sorted_device_ptr_list[idx]->device.name, sorted_device_ptr_list[idx], &device_menu_item_attr);
         }
      }
   }
   
   free(sorted_device_ptr_list);
   rtv_level = RTV_DEVICE_MENU;
   return 0;
}

// replaytv_show_device_menu
//
int replaytv_show_device_menu(void)
{
   mvpw_show(replaytv_logo);
   mvpw_show(rtv_device_menu);
   mvpw_focus(rtv_device_menu);
   return(0);
}

// Hide all widgets
//
int replaytv_hide_device_menu(void)
{
   mvpw_hide(replaytv_logo);
   mvpw_hide(rtv_device_menu);
   mvpw_hide(rtv_device_descr.container);
   return(0);
}

// called from video.c when back/exit or stop pressed
//
void replaytv_back_from_video(void)
{
   video_stop_play();
   rtv_abort_read();
   video_clear();               //kick video.c
   mvpw_show(replaytv_logo);
   mvpw_show(rtv_browser);
   mvpw_show(rtv_episode_description);
   mvpw_focus(rtv_browser);
   rtv_level = RTV_BROWSER_MENU;
}

// replaytv_osd_proginfo_update()
// called from video.c
//
void replaytv_osd_proginfo_update(mvp_widget_t *widget)
{  
   static const rtv_show_export_t *current_show_p = NULL;

   if ( current_show_p != rtv_video_state.show_p ) {
      char descr[512];
      char *pos = descr;

      current_show_p = rtv_video_state.show_p;
      
      mvpw_set_text_str(rtv_osd_show_title_widget, current_show_p->title);
      mvpw_expose(rtv_osd_show_title_widget);
      

      if ( strcmp(current_show_p->episode, "") != 0 ) {
         pos += sprintf(pos, "%s", current_show_p->episode);
         if ( strcmp(current_show_p->description, "") != 0 ) {
            pos += sprintf(pos, ": ");
         }
      }        
      if ( strcmp(current_show_p->description, "") != 0 ) {
         pos += sprintf(pos, "%s ", current_show_p->description);
      }

      mvpw_set_text_str(rtv_osd_show_descr_widget, descr);
      mvpw_expose(rtv_osd_show_descr_widget);
   }
}

// replaytv gui init
//
int replay_gui_init(void)
{
   char               logo_file[128];
   mvpw_image_info_t  iid;
   mvpw_widget_info_t wid, wid2;
   int x, y, w, h, i, container_y;
   
   mvpw_get_screen_info(&scr_info);
   rtv_default_menu_attr.font = fontid;
   
   // init replaytv logo image
   //
   snprintf(logo_file, sizeof(logo_file), "%s/replaytv1_rotate.png", imagedir);
   if (mvpw_get_image_info(logo_file, &iid) < 0) {
      return -1;
   }
   replaytv_logo = mvpw_create_image(NULL, 50, 15, iid.width, iid.height, 0, 0, 0);
   mvpw_set_image(replaytv_logo, logo_file);
   
   // init device selection menu
   //
   rtv_device_menu = mvpw_create_menu(NULL, 50+iid.width, 30, scr_info.cols-120-iid.width, 
                                      scr_info.rows-220, 0xff808080, 0xff606060, 2);
   
   mvpw_set_menu_attr(rtv_device_menu, &rtv_default_menu_attr);
   mvpw_set_key(rtv_device_menu, rtv_device_menu_callback);
   
   // init show browser 
   //
   rtv_browser = mvpw_create_menu(NULL, 50+iid.width, 30, scr_info.cols-120-iid.width,
                                  scr_info.rows-220, 0xff808080, 0xff606060, 2);
   
   mvpw_set_menu_attr(rtv_browser, &rtv_default_menu_attr);
   mvpw_set_key(rtv_browser, rtv_browser_key_callback);
   
   // init show-browser-episode-info window
   //
   mvpw_get_widget_info(replaytv_logo, &wid);
   mvpw_get_widget_info(rtv_browser, &wid2);
   //printf("logo: x=%d y=%d w=%d h=%d\n", wid.x, wid.y, wid.w, wid.h);
   //printf("brow: x=%d y=%d w=%d h=%d\n", wid2.x, wid2.y, wid2.w, wid2.h);

   rtv_episode_descr_attr.font = fontid;

   x = wid.x + 20; // logo x
   y = ((wid.y + wid.h) > (wid2.y + wid2.h)) ?  (wid.y + wid.h) : (wid2.y + wid2.h); // whoever goes farthest down
   y += 10;
   container_y = y;
   w = scr_info.cols - x - 50; // width between logo x:start and right side of screen
   h = mvpw_font_height(rtv_episode_descr_attr.font);
   //printf("si: x=%d y=%d w=%d h=%d\n", x, y, w, h);

   // Set up NUM_EPISODE_LINES for episode description and place in a container widget 
   //
	rtv_episode_description = mvpw_create_container(NULL, x, container_y, w, h*NUM_EPISODE_LINES, 0x80000000, 0, 0);
   for ( i=0; i < NUM_EPISODE_LINES; i++ ) {
      rtv_episode_line[i] = mvpw_create_text(rtv_episode_description, 0, h*i, w, h, 0, 0, 0);	
      mvpw_set_text_attr(rtv_episode_line[i], &rtv_episode_descr_attr);
      mvpw_set_text_str(rtv_episode_line[i], "");
      mvpw_show(rtv_episode_line[i]);
      y+=h;
   }

   // init device-browser-info window
   //
   mvpw_get_widget_info(replaytv_logo, &wid);
   mvpw_get_widget_info(rtv_device_menu, &wid2);
   
   rtv_device_descr_attr.font = fontid;

   x = wid.x + 20; // logo x
   y = ((wid.y + wid.h) > (wid2.y + wid2.h)) ?  (wid.y + wid.h) : (wid2.y + wid2.h); // whoever goes farthest down
   y += 10;
   container_y = y;
   w = scr_info.cols - x - 50; // width between logo x:start and right side of screen
   h = mvpw_font_height(rtv_device_descr_attr.font);

	rtv_device_descr.container = mvpw_create_container(NULL, x, container_y + h, w, h*3, 0x00000000, 0, 0);

   rtv_device_descr.name = mvpw_create_text(rtv_device_descr.container, 0, h*0, w/2, h, 0, 0, 0);	
   mvpw_set_text_attr(rtv_device_descr.name, &rtv_device_descr_attr);
   mvpw_set_text_str(rtv_device_descr.name, "");
   mvpw_show(rtv_device_descr.name);

   rtv_device_descr.model = mvpw_create_text(rtv_device_descr.container, 0, h*1, w/2, h, 0, 0, 0);	
   mvpw_set_text_attr(rtv_device_descr.model, &rtv_device_descr_attr);
   mvpw_set_text_str(rtv_device_descr.model, "");
   mvpw_show(rtv_device_descr.model);

   rtv_device_descr.ipaddr = mvpw_create_text(rtv_device_descr.container, 0, h*2, w/2, h, 0, 0, 0);	
   mvpw_set_text_attr(rtv_device_descr.ipaddr, &rtv_device_descr_attr);
   mvpw_set_text_str(rtv_device_descr.ipaddr, "");
   mvpw_show(rtv_device_descr.ipaddr);

   rtv_device_descr.capacity = mvpw_create_text(rtv_device_descr.container, w/2, h*0, w/2, h, 0, 0, 0);	
   mvpw_set_text_attr(rtv_device_descr.capacity, &rtv_device_descr_attr);
   mvpw_set_text_str(rtv_device_descr.capacity, "");
   mvpw_show(rtv_device_descr.capacity);

   i = mvpw_font_width(rtv_device_descr_attr.font, "XXX%");
   rtv_device_descr.percentage = mvpw_create_text(rtv_device_descr.container, w-20-i, h*0, i, h, 0, 0, 0);	
   mvpw_set_text_attr(rtv_device_descr.percentage, &rtv_device_descr_attr);
   mvpw_set_text_str(rtv_device_descr.percentage, "");
   mvpw_show(rtv_device_descr.percentage);
   
   rtv_device_descr.graph = mvpw_create_graph(rtv_device_descr.container, w/2, h*1, (w/2)-20, h, MVPW_LIGHTGREY, 0, 0);
	mvpw_set_graph_attr(rtv_device_descr.graph, &discspace_graph_attr);
   mvpw_show(rtv_device_descr.graph);

   rtv_device_descr.inuse = mvpw_create_text(rtv_device_descr.container, w/2, h*2, w/4, h, 0, 0, 0);	
   mvpw_set_text_attr(rtv_device_descr.inuse, &rtv_device_descr_attr);
   mvpw_set_text_str(rtv_device_descr.inuse, "");
   mvpw_show(rtv_device_descr.inuse);
   
   rtv_device_descr.free = mvpw_create_text(rtv_device_descr.container, w-(w/4), h*2, w/4, h, 0, 0, 0);	
   mvpw_set_text_attr(rtv_device_descr.free, &rtv_device_descr_attr);
   mvpw_set_text_str(rtv_device_descr.free, "");
   mvpw_show(rtv_device_descr.free);
   

   // init OSD program(show) info window
   //
   rtv_osd_show_title_attr.font = fontid;
   rtv_osd_show_desc_attr.font  = fontid;
   x = 50;
   y = scr_info.rows - 125;

   rtv_osd_proginfo_widget = mvpw_create_container(NULL, x, y, scr_info.cols-125, h*3, 0x80000000, 0, 0);
   rtv_osd_show_title_widget = mvpw_create_text(rtv_osd_proginfo_widget, 0, 0, scr_info.cols-125, h, 0x80000000, 0, 0);
   mvpw_set_text_attr(rtv_osd_show_title_widget, &rtv_osd_show_title_attr);
   mvpw_set_text_str(rtv_osd_show_title_widget, "");
   mvpw_show(rtv_osd_show_title_widget);

   rtv_osd_show_descr_widget = mvpw_create_text(rtv_osd_proginfo_widget, 0, 0, scr_info.cols-125, h*2, 0x80000000, 0, 0);
   mvpw_set_text_attr(rtv_osd_show_descr_widget, &rtv_osd_show_desc_attr);
   mvpw_set_text_str(rtv_osd_show_descr_widget, "");
   mvpw_show(rtv_osd_show_descr_widget);
   mvpw_attach(rtv_osd_show_title_widget, rtv_osd_show_descr_widget, MVPW_DIR_DOWN);

   // int the show browser popup window
   //
	w = 200;
	h = 200;
	x = (si.cols - w) * 2 / 3;
	y = (si.rows - h) / 4;

	rtv_popup = mvpw_create_menu(NULL, x, y, w, h,
                                MVPW_BLACK, MVPW_GREEN, 2);
   
	rtv_popup_attr.font = fontid;
	mvpw_set_menu_attr(rtv_popup, &rtv_popup_attr);
   
	mvpw_set_menu_title(rtv_popup, "Show Menu");
	mvpw_set_bg(rtv_popup, MVPW_BLACK);   
	mvpw_set_key(rtv_popup, sub_window_key_callback);
   
   // init jump_to_time window
   //
   rtv_jump_to_time_attr.font = fontid;
   h = mvpw_font_height(rtv_jump_to_time_attr.font);
   w = mvpw_font_width(rtv_jump_to_time_attr.font, " 555 JUMP ");
	rtv_jump_to_time_widget = mvpw_create_text(NULL, 50, 25, w, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(rtv_jump_to_time_widget, &rtv_jump_to_time_attr);
	mvpw_set_text_str(rtv_jump_to_time_widget, "");
	mvpw_attach(clock_widget, rtv_jump_to_time_widget, MVPW_DIR_RIGHT);

   //
   mvpw_raise(rtv_browser);
   mvpw_raise(rtv_device_menu);
	mvpw_raise(rtv_popup);
   return(0);
}
