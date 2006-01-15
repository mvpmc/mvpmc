#ifndef MVPMC_H
#define MVPMC_H

/*
 *  $Id$
 *
 *  Copyright (C) 2004, 2005, Jon Gettler
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
	MVPMC_STATE_NONE = 1,
	MVPMC_STATE_MYTHTV,
	MVPMC_STATE_FILEBROWSER,
	MVPMC_STATE_REPLAYTV,
	MVPMC_STATE_MCLIENT,
} mvpmc_state_t;

typedef enum {
	MYTHTV_STATE_MAIN,
	MYTHTV_STATE_PENDING,
	MYTHTV_STATE_PROGRAMS,
	MYTHTV_STATE_EPISODES,
	MYTHTV_STATE_LIVETV,
	MYTHTV_STATE_UPCOMING,
} mythtv_state_t;

typedef enum {
	MYTHTV_FILTER_NONE,
	MYTHTV_FILTER_TITLE,
	MYTHTV_FILTER_RECORD,
	MYTHTV_FILTER_RECORD_CONFLICT,
} mythtv_filter_t;

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

#define MYTHTV_NUM_SORTS 2
typedef enum {
	MYTHTV_SORT_DATE_RECORDED = 0,
	MYTHTV_SORT_ORIGINAL_AIRDATE,
} mvp_myth_sort;

#define MYTHTV_NUM_SORTS_PROGRAMS 3
typedef enum {
	SHOW_TITLE,
	SHOW_CATEGORY,
	SHOW_RECGROUP,
} show_sort_t;

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
	int (*halt_stream)(void);          // Notify client to halt streaming
} video_callback_t;

typedef struct playlist_struct playlist_t;

struct playlist_struct {
  char *filename;
  char *name;
  int seconds;
  void *key;
  struct playlist_struct *next;
  struct playlist_struct *prev;
};

extern void playlist_create(char **item, int n, char *cwd);

extern volatile video_callback_t *video_functions;
extern video_callback_t file_functions;

extern volatile int video_playing;

extern char *replaytv_server;
extern char *mythtv_server;
extern int mythtv_debug;
extern volatile mythtv_state_t mythtv_state;

extern mvp_widget_t *file_browser;
extern mvp_widget_t *mythtv_browser;
extern mvp_widget_t *mythtv_menu;
extern mvp_widget_t *mythtv_logo;
extern mvp_widget_t *mythtv_date;
extern mvp_widget_t *mythtv_description;
extern mvp_widget_t *mythtv_channel;
extern mvp_widget_t *mythtv_record;
extern mvp_widget_t *mythtv_popup;
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

extern mvp_widget_t *wss_16_9_image;
extern mvp_widget_t *wss_4_3_image;

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

extern mvp_widget_t *playlist_widget;

extern mvp_widget_t *fb_program_widget;
extern mvp_widget_t *fb_progress;
extern mvp_widget_t *fb_offset_widget;
extern mvp_widget_t *fb_offset_bar;
extern mvp_widget_t *fb_name;
extern mvp_widget_t *fb_time;
extern mvp_widget_t *fb_size;
extern mvp_widget_t *vnc_widget;

extern mvp_widget_t *volume_dialog;

extern void volume_key_callback(mvp_widget_t *widget, char key);

extern void timer_hide(mvp_widget_t *widget);

extern uint32_t root_color;
extern int root_bright;
extern int volume;

extern char *current;
extern char *current_hilite;
extern char *mythtv_recdir;
extern char *mythtv_ringbuf;

extern playlist_t *playlist;

extern char *imagedir;

extern int fontid;
extern mvpw_screen_info_t si;

extern int running_mythtv;
extern int mythtv_main_menu;
extern int mythtv_tcp_control;
extern int mythtv_tcp_program;
extern int mythtv_sort;
extern int mythtv_sort_dirty;

extern int fd_audio, fd_video;
extern int fd;

extern volatile int seeking;
extern volatile int jumping;
extern volatile int paused;
extern volatile int audio_playing;
extern volatile int audio_stop;

extern int video_init(void);
extern void audio_play(mvp_widget_t*);
extern void video_play(mvp_widget_t*);
extern void video_set_root(void);
extern void playlist_play(mvp_widget_t*);
extern void playlist_next();

extern int mythtv_init(char*, int);
extern void mythtv_atexit(void);
extern int gui_init(char*, char*);
extern int mw_init(char *server, char *replaytv);
extern int display_init(void);

extern void audio_clear(void);
extern void video_clear(void);
extern void playlist_clear(void);
extern void video_stop_play(void);

extern int mythtv_back(mvp_widget_t*);
extern int fb_update(mvp_widget_t*);
extern int mythtv_update(mvp_widget_t*);
extern int mythtv_livetv_menu(void);
extern int mythtv_program_runtime(void);
extern void mythtv_set_popup_menu(mythtv_state_t state);

extern void video_callback(mvp_widget_t*, char);
extern void video_thumbnail(int on);

extern void mythtv_show_widgets(void);
extern void mythtv_program(mvp_widget_t *widget);

extern void fb_program(mvp_widget_t *widget);
extern void fb_shuffle(void);

extern void re_exec(void);
extern void power_toggle(void);

extern int audio_switch_stream(mvp_widget_t*, int);
extern void video_switch_stream(mvp_widget_t*, int);
extern void add_audio_streams(mvp_widget_t*, mvpw_menu_item_attr_t*);
extern void add_video_streams(mvp_widget_t*, mvpw_menu_item_attr_t*);

extern void *video_read_start(void*);
extern void *video_write_start(void*);
extern void *audio_write_start(void*);
extern void *display_thread(void*);
extern void* audio_start(void *arg);

extern void replaytv_back_to_mvp_main_menu(void);

extern int is_video(char *item);
extern int is_audio(char *item);
extern int is_image(char *item);

extern void gui_error(char*);
extern void gui_mesg(char*, char*);

extern pthread_t video_write_thread;
extern pthread_t audio_write_thread;
extern pthread_t audio_thread;
extern pthread_attr_t thread_attr, thread_attr_small;

extern pthread_cond_t video_cond;
extern pthread_cond_t mclient_cond;

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

	if (!osd_widgets[i].visible) {
		mvpw_set_timer(osd_widgets[i].widget, NULL, 1000);
		mvpw_hide(osd_widgets[i].widget);
		return 0;
	}

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
extern volatile int screensaver_timeout;
extern volatile int screensaver_default;

extern volatile int mythtv_livetv;

extern char vnc_server[256];
extern int vnc_port;

extern char compile_time[], version[];

extern av_demux_mode_t demux_mode;

extern demux_handle_t *handle;

extern int (*DEMUX_PUT)(demux_handle_t*, void*, int);
extern int (*DEMUX_WRITE_VIDEO)(demux_handle_t*, int);
extern int (*DEMUX_WRITE_AUDIO)(demux_handle_t*, int);

extern int mythtv_livetv_stop(void);
extern int mythtv_channel_up(void);
extern int mythtv_channel_down(void);

extern void empty_ac3(void);

extern void gui_error_clear(void);

extern void mythtv_cleanup(void);
extern void mythtv_stop(void);
extern int mythtv_delete(void);
extern int mythtv_forget(void);
extern int mythtv_proginfo(char *buf, int size);
extern void mythtv_start_thumbnail(void);
extern int mythtv_pending(mvp_widget_t *widget);
extern int mythtv_pending_filter(mvp_widget_t *widget, mythtv_filter_t filter);
extern void mythtv_test_exit(void);
extern int mythtv_proginfo_livetv(char *buf, int size);
extern int mythtv_livetv_tuners(int*, int*);
extern void mythtv_livetv_select(int);
extern void mythtv_thruput(void);

extern void playlist_prev(void);
extern void playlist_stop(void);

extern void subtitle_switch_stream(mvp_widget_t*, int);
extern void add_subtitle_streams(mvp_widget_t*, mvpw_menu_item_attr_t*);

extern av_passthru_t audio_output_mode;

extern void busy_start(void);
extern void busy_end(void);

extern void fb_start_thumbnail(void);
extern void fb_thruput(void);

typedef enum {
	WIDGET_DIALOG,
	WIDGET_GRAPH,
	WIDGET_MENU,
	WIDGET_TEXT,
} widget_t;

typedef struct {
	char *name;
	widget_t type;
	union {
		mvpw_dialog_attr_t *dialog;
		mvpw_graph_attr_t *graph;
		mvpw_menu_attr_t *menu;
		mvpw_text_attr_t *text;
	} attr;
} theme_attr_t;

extern theme_attr_t theme_attr[];

extern int theme_parse(char *file);

extern int seek_osd_timeout;
extern int pause_osd;

typedef struct {
	char	*name;
	char	*path;
	int	 current;
} theme_t;

extern theme_t theme_list[];

#define THEME_MAX	32
#define DEFAULT_THEME	"/usr/share/mvpmc/mvpmc_current_theme.xml"
#define MASTER_THEME	"/usr/share/mvpmc/theme.xml"

#define DEFAULT_FONT	"/etc/helvR10.fnt"

typedef struct {
	uint32_t livetv_current;
	uint32_t pending_will_record;
	uint32_t pending_conflict;
	uint32_t pending_recording;
	uint32_t pending_other;
} mythtv_color_t;

extern mythtv_color_t mythtv_colors;

typedef struct {
	int bitrate;
	int clock;
	int demux_info;
	int progress;
	int program;
	int timecode;
} osd_settings_t;

extern osd_settings_t osd_settings;

extern void switch_hw_state(mvpmc_state_t new);
extern void switch_gui_state(mvpmc_state_t new);

extern void fb_exit(void);
extern void mythtv_exit(void);
extern void replaytv_exit(void);
extern void replaytv_atexit(void);

extern mvpmc_state_t hw_state;
extern mvpmc_state_t gui_state;

extern char *screen_capture_file;

extern mvp_widget_t *ct_text_box;
extern mvp_widget_t *settings;

extern int settings_disable;
extern int reboot_disable;
extern int filebrowser_disable;

extern unsigned short viewport_edges[4];

extern void mythtv_browser_expose(mvp_widget_t *widget);

extern void start_thruput_test(void);
extern void end_thruput_test(void);
extern mvp_widget_t *thruput_widget;

extern show_sort_t show_sort;
#endif /* MVPMC_H */
