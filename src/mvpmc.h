#ifndef MVPMC_H
#define MVPMC_H

/*
 *  $Id$
 *
 *  Copyright (C) 2004, Jon Gettler
 *  http://mvpmc.sourceforge.net/
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

#define VIDEO_BUFF_SIZE	(1024*96)

typedef enum {
	OSD_BITRATE = 1,
	OSD_CLOCK,
	OSD_DEMUX,
	OSD_PROGRESS,
	OSD_PROGRAM,
	OSD_TIMECODE,
} osd_type_t;

typedef enum {
   MVP_READ_THREAD_IDLE = 1,
} mvp_notify_t;

typedef struct {
	int type;
	int visible;
	mvp_widget_t *widget;
	void (*callback)(mvp_widget_t *widget);
} osd_widget_t;

typedef struct {
	int (*open)(void);
	int (*read)(char*, int);           // For read functions that fill in a buffer passed by the caller
	int (*read_dynb)(char**, int);     // For read functions that return a pointer to a dynamic buffer
	long long (*seek)(long long, int);
	long long (*size)();               // Current mpeg file size
	void (*notify)(mvp_notify_t);      // For mvp code to notify client of whatever
	int (*key)(char);                  // Client specific handling of keypresses during video play.
                                      // Client should return 1 if it handled the keypress. Else return 0
} video_callback_t;

typedef struct playlist_struct playlist_t;

struct playlist_struct {
  char *filename;
  struct playlist_struct *next;
};

extern volatile video_callback_t *video_functions;
extern video_callback_t file_functions, mythtv_functions;

extern char *mythtv_server;
extern int mythtv_debug;
extern volatile int mythtv_level;

extern mvp_widget_t *file_browser;
extern mvp_widget_t *mythtv_browser;
extern mvp_widget_t *mythtv_menu;
extern mvp_widget_t *mythtv_logo;
extern mvp_widget_t *mythtv_date;
extern mvp_widget_t *mythtv_description;
extern mvp_widget_t *mythtv_channel;
extern mvp_widget_t *mythtv_record;
extern mvp_widget_t *root;
extern mvp_widget_t *iw;
extern mvp_widget_t *pause_widget;
extern mvp_widget_t *mute_widget;
extern mvp_widget_t *ffwd_widget;
extern mvp_widget_t *zoom_widget;
extern mvp_widget_t *osd_widget;
extern mvp_widget_t *offset_widget;
extern mvp_widget_t *offset_bar;
extern mvp_widget_t *bps_widget;
extern mvp_widget_t *spu_widget;

extern mvp_widget_t *episodes_widget;
extern mvp_widget_t *shows_widget;
extern mvp_widget_t *freespace_widget;

extern mvp_widget_t *popup_menu;

extern mvp_widget_t *time_widget;
extern mvp_widget_t *mythtv_program_widget;
extern mvp_widget_t *mythtv_osd_program;
extern mvp_widget_t *mythtv_osd_description;
extern mvp_widget_t *clock_widget;
extern mvp_widget_t *demux_video;
extern mvp_widget_t *demux_audio;

extern char *current;
extern char *mythtv_recdir;
extern char *mythtv_ringbuf;

extern playlist_t *playlist;

extern char *imagedir;

extern int fontid;
extern mvpw_screen_info_t si;

extern int running_mythtv;
extern int mythtv_main_menu;

extern int fd_audio, fd_video;

extern volatile int seeking;
extern volatile int jumping;
extern volatile int paused;

extern int video_init(void);
extern void audio_play(mvp_widget_t*);
extern void video_play(mvp_widget_t*);
extern void playlist_play(mvp_widget_t*);
extern void playlist_next();

extern int mythtv_init(char*, int);
extern int gui_init(char*, char*);
extern int mw_init(char *server, char *replaytv);

extern void audio_clear(void);
extern void video_clear(void);
extern void playlist_clear(void);
extern void video_stop_play(void);

extern int mythtv_back(mvp_widget_t*);
extern int fb_update(mvp_widget_t*);
extern int mythtv_update(mvp_widget_t*);

extern void video_callback(mvp_widget_t*, char);

extern void mythtv_show_widgets(void);
extern void mythtv_program(mvp_widget_t *widget);

extern void re_exec(void);
extern void power_toggle(void);

extern void audio_switch_stream(mvp_widget_t*, int);
extern void video_switch_stream(mvp_widget_t*, int);
extern void add_audio_streams(mvp_widget_t*, mvpw_menu_item_attr_t*);
extern void add_video_streams(mvp_widget_t*, mvpw_menu_item_attr_t*);

extern void *video_read_start(void*);
extern void *video_write_start(void*);
extern void *audio_write_start(void*);

extern void replaytv_back_to_mvp_main_menu(void);

extern int is_video(char *item);
extern int is_audio(char *item);
extern int is_image(char *item);

extern pthread_t video_write_thread;
extern pthread_t audio_write_thread;

extern pthread_cond_t video_cond;

extern void add_osd_widget(mvp_widget_t *widget, int type, int visible,
			   void (*callback)(mvp_widget_t*));

#define MAX_OSD_WIDGETS		8

extern osd_widget_t osd_widgets[];

extern inline int
set_osd_callback(int type, void (*callback)(mvp_widget_t*))
{
	int i = 0;

	while ((osd_widgets[i].type != type) && (i < MAX_OSD_WIDGETS))
		i++;

	if (i == MAX_OSD_WIDGETS)
		return -1;

	osd_widgets[i].callback = callback;

	if ((! osd_widgets[i].visible) && callback)
		return 0;

	mvpw_set_timer(osd_widgets[i].widget, callback, 1000);

	if (callback) {
		callback(osd_widgets[i].widget);
		mvpw_show(osd_widgets[i].widget);
	} else {
		mvpw_hide(osd_widgets[i].widget);
	}

	return 0;
}

extern int a52_decode_data (uint8_t * start, uint8_t * end, int reset);

extern void screensaver_enable(void);
extern void screensaver_disable(void);

extern int mythtv_livetv;

#endif /* MVPMC_H */
