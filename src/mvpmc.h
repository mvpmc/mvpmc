#ifndef MVPMC_H
#define MVPMC_H

/*
 *  Copyright (C) 2004, 2005, 2006, Jon Gettler
 *  http://www.mvpmc.org/
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

/** \file mvpmc.h
 * Global definitions for the mvpmc application.
 */

#define VIDEO_BUFF_SIZE	(1024*96)	/**< video input buffer size */

/**
 * Portion of the mvpmc application which owns either the gui or
 * the audio/video playback hardware. 
 * Note: shutdown state used when previous application requires 
 * delays when turning off.
 */
typedef enum {
	MVPMC_STATE_NONE = 1,		/**< no state */
	MVPMC_STATE_MYTHTV,		/**< mythtv */
	MVPMC_STATE_MYTHTV_SHUTDOWN,	/**< mythtv shutdown */
	MVPMC_STATE_FILEBROWSER,	/**< filebrowser */
	MVPMC_STATE_FILEBROWSER_SHUTDOWN,	/**< filebrowser shutdown */
	MVPMC_STATE_REPLAYTV,		/**< replaytv */
	MVPMC_STATE_REPLAYTV_SHUTDOWN,	/**< replaytv shutdown */
	MVPMC_STATE_MCLIENT,		/**< slimserver mclient */
	MVPMC_STATE_MCLIENT_SHUTDOWN,	/**< slimserver mclient shutdown */
	MVPMC_STATE_HTTP,		/**< http */
	MVPMC_STATE_HTTP_SHUTDOWN,	/**< http shutdown */
	MVPMC_STATE_EMULATE,		/**< hauppauge emulation */
	MVPMC_STATE_EMULATE_SHUTDOWN,	/**< hauppauge emulation shutdown */
} mvpmc_state_t;

/**
 * On-Screen-Display options
 */
typedef enum {
	OSD_BITRATE = 1,		/**< bitrate */
	OSD_CLOCK,			/**< clock */
	OSD_DEMUX,			/**< audio/video demux graphs */
	OSD_PROGRESS,			/**< progress meter */
	OSD_PROGRAM,			/**< program description */
	OSD_TIMECODE,			/**< timecode */
} vid_osd_type_t;

/**
 * Playlist menu items
 */
typedef enum {
	PL_SHUFFLE = 1,
	PL_REPEAT,
	PL_VOLUME,
} playlist_menu_t;

/**
 * video stream client events
 */
typedef enum {
	MVP_READ_THREAD_IDLE = 1,
} mvp_notify_t;

/**
 * On-Screen-Display settings
 */
typedef struct {
	int type;				/**< widget type */
	int visible;				/**< visible or not */
	mvp_widget_t *widget;			/**< widget handle */
	void (*callback)(mvp_widget_t *widget);	/**< expose callback */
} osd_widget_t;

/**
 * Video playback callbacks.
 */
typedef struct {
	/**
	 * open a video stream
	 * \retval 0 success
	 * \retval -1 error
	 */
	int (*open)(void);
	/**
	 * add video stream data to a buffer
	 * \param[out] buf data buffer
	 * \param len data buffer length
	 * \return amount of data read
	 */
	int (*read)(char *buf, int len);
	/**
	 * return video stream data in a buffer
	 * \param[out] bufp pointer to callee allocated buffer
	 * \param len max amount of data to return
	 * \return amount of data read
	 */
	int (*read_dynb)(char **bufp, int len);
	/**
	 * seek to a certain position in the video stream
	 * \param offset stream offset
	 * \param whence SEEK_SET, SEEK_CUR, SEEK_END
	 * \return new offset from the beginning of the stream
	 */
	long long (*seek)(long long offset, int whence);
	/**
	 * get the current size of the video stream
	 * \return video stream size
	 */
	long long (*size)(void);
	/**
	 * notify the video stream client of an event
	 * \param event event type
	 */
	void (*notify)(mvp_notify_t event);
	/**
	 * notify the video stream client of a keypress
	 * \param key key pressed
	 * \retval 0 key was not handled by the client
	 * \retval 1 key was handled by the client
	 */
	int (*key)(char key);
	/**
	 * notify the client to halt the stream
	 * \retval 0 success
	 */
	int (*halt_stream)(void);
} video_callback_t;

typedef struct playlist_struct playlist_t;

/**
 * playlist item
 */
struct playlist_struct {
	char *filename;			/**< filename */
	char *name;			/**< ID3 or M3U name */
	char *label;			/**< widget label */
	int seconds;			/**< length of recording */
	void *key;			/**< widget key */
	struct playlist_struct *next;	/**< next playlist item */
	struct playlist_struct *prev;	/**< previous playlist item */
};

/**
 * Create a playlist.
 * \param item playlist item list
 * \param n number of items
 * \param cwd basename for files in the playlist
 */
extern void playlist_create(char **item, int n, char *cwd);

/**
 * Video playback functions for the current streaming method.
 */
extern volatile video_callback_t *video_functions;

/**
 * Video playback functions for playing files from the filesystem.
 */
extern video_callback_t file_functions;

/**
 * Video playback functions for playing files from the filesystem via VLC
 */
extern video_callback_t vlc_functions;

/**
 * Is a video currently playing?
 */
extern volatile int video_playing;

/**
 * ReplayTV server hostname or IP address.
 */
extern char *replaytv_server;

/**
 * Hauppauge server hostname or IP address.
 */
extern char *mvp_server;

extern mvp_widget_t *file_browser;	/**< file browser */
extern mvp_widget_t *root;		/**< root window */
extern mvp_widget_t *iw;		/**< image viewer */
extern mvp_widget_t *pause_widget;	/**< pause */
extern mvp_widget_t *mute_widget;	/**< mute */
extern mvp_widget_t *ffwd_widget;	/**< fast forward */
extern mvp_widget_t *zoom_widget;	/**< zoom */
extern mvp_widget_t *osd_widget;	/**< On-Screen-Display */
extern mvp_widget_t *offset_widget;	/**< file percentage */
extern mvp_widget_t *offset_bar;	/**< file percentage graph */
extern mvp_widget_t *bps_widget;	/**< bits per second */
extern mvp_widget_t *spu_widget;	/**< sub-picture */

extern mvp_widget_t *episodes_widget;
extern mvp_widget_t *shows_widget;
extern mvp_widget_t *freespace_widget;
extern mvp_widget_t *program_info_widget;
extern mvp_widget_t *mythtv_options;

extern mvp_widget_t *popup_menu;

extern mvp_widget_t *time_widget;
extern mvp_widget_t *clock_widget;
extern mvp_widget_t *demux_video;
extern mvp_widget_t *demux_audio;

extern mvp_widget_t *playlist_widget;
extern mvp_widget_t *pl_menu;

extern mvp_widget_t *fb_program_widget;
extern mvp_widget_t *fb_progress;
extern mvp_widget_t *fb_offset_widget;
extern mvp_widget_t *fb_offset_bar;
extern mvp_widget_t *fb_name;
extern mvp_widget_t *fb_time;
extern mvp_widget_t *fb_size;
extern mvp_widget_t *vnc_widget;

extern mvp_widget_t *volume_dialog;

extern mvp_widget_t *mclient_fullscreen;
extern mvp_widget_t *mclient_sub_softsqueeze;
extern mvp_widget_t *mclient_sub_image;
extern mvp_widget_t *mclient_sub_image_1_1;
extern mvp_widget_t *mclient_sub_image_1_2;
extern mvp_widget_t *mclient_sub_image_1_3;
extern mvp_widget_t *mclient_sub_image_2_1;
extern mvp_widget_t *mclient_sub_image_2_2;
extern mvp_widget_t *mclient_sub_image_2_3;
extern mvp_widget_t *mclient_sub_image_3_1;
extern mvp_widget_t *mclient_sub_image_3_2;
extern mvp_widget_t *mclient_sub_image_3_3;
extern mvp_widget_t *mclient_sub_alt_image_1_1;
extern mvp_widget_t *mclient_sub_alt_image_1_2;
extern mvp_widget_t *mclient_sub_alt_image_1_3;
extern mvp_widget_t *mclient_sub_alt_image_2_1;
extern mvp_widget_t *mclient_sub_alt_image_2_2;
extern mvp_widget_t *mclient_sub_alt_image_2_3;
extern mvp_widget_t *mclient_sub_alt_image_3_1;
extern mvp_widget_t *mclient_sub_alt_image_3_2;
extern mvp_widget_t *mclient_sub_alt_image_3_3;
extern mvp_widget_t *mclient_sub_alt_image_info;
extern mvp_widget_t *mclient_sub_alt_image;
extern mvp_widget_t *mclient_sub_progressbar;
extern mvp_widget_t *mclient_sub_volumebar;
extern mvp_widget_t *mclient_sub_browsebar;
extern mvp_widget_t *mclient_sub_localmenu;

extern void volume_key_callback(mvp_widget_t *widget, char key);

extern void timer_hide(mvp_widget_t *widget);

extern uint32_t root_color;
extern int root_bright;
extern int volume;

extern char *current;
extern char *current_hilite;

extern playlist_t *playlist;
extern int playlist_repeat;

extern char *imagedir;

extern int fontid;
extern mvpw_screen_info_t si;

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
extern void playlist_randomize(void);
extern void timed_osd(int timeout); 
extern int video_get_byterate(void);

extern int file_open(void);
extern int file_read(char*, int);
extern volatile long long jump_target;
extern int display_on;
extern int display_on_alt;
extern void display_bookmark_status_osd(int);
extern void enable_osd(void);
extern void disable_osd(void);
extern void back_to_guide_menu();

extern int gui_init(char*, char*);
extern int mw_init(void);
extern int display_init(void);

extern void audio_clear(void);
extern void video_clear(void);
extern void playlist_clear(void);
extern void video_stop_play(void);

extern int fb_update(mvp_widget_t*);

extern void video_callback(mvp_widget_t*, char);
extern void video_thumbnail(int on);

extern void fb_program(mvp_widget_t *widget);
extern void fb_shuffle(int);

extern void re_exec(void);
extern void power_toggle(void);

extern int audio_switch_stream(mvp_widget_t*, int);
extern void video_switch_stream(mvp_widget_t*, int);
extern void add_audio_streams(mvp_widget_t*, mvpw_menu_item_attr_t*);
extern void add_video_streams(mvp_widget_t*, mvpw_menu_item_attr_t*);

extern void *av_sync_start(void*);
extern void *video_events_start(void*);
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

static inline int
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

extern char vnc_server[256];
extern int vnc_port;

extern char compile_time[], version_number[], build_user[];
extern char git_revision[], git_diffs[];
extern char *version;

extern av_demux_mode_t demux_mode;

extern demux_handle_t *handle;

extern int (*DEMUX_PUT)(demux_handle_t*, void*, int);
extern int (*DEMUX_WRITE_VIDEO)(demux_handle_t*, int);
extern int (*DEMUX_WRITE_AUDIO)(demux_handle_t*, int);

extern void empty_ac3(void);

extern void gui_error_clear(void);

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

// Stream input buffer size for http/vlc
#define  LINE_SIZE 1024 

// VLC specifics 
#define VLC_VLM_PORT "4212"
#define VLC_HTTP_PORT "5212"

// VLC command types
typedef enum {
        VLC_CREATE_BROADCAST,
        VLC_CONTROL,
	VLC_PCTPOS,
	VLC_DESTROY,
	VLC_SEEK_PCT,
	VLC_SEEK_SEC,
} vlc_command_type_t;

extern int using_vlc;
extern int vlc_broadcast_enabled;
extern char *vlc_server;
extern int vlc_connect(FILE *outlog,char *url,int ContentType, int VlcCommandType, char *VlcCommandArg, int offset);
extern int vlc_stop();
extern int vlc_destroy();
extern int vlc_cmd(char *cmd);
extern int vlc_get_pct_pos();
extern int vlc_seek_pct(int pos);
extern int vlc_seek_pct_relative(int offset);
extern int vlc_seek_sec_relative(int offset);
extern void vlc_timecode(char* timecode);

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
extern int startup_this_feature;

extern unsigned short viewport_edges[4];

extern void start_thruput_test(void);
extern void end_thruput_test(void);
extern mvp_widget_t *thruput_widget;

enum {
	MM_EXIT,
	MM_MYTHTV,
	MM_FILESYSTEM,
	MM_ABOUT,
	MM_VNC,
	MM_SETTINGS,
	MM_REPLAYTV,
	MM_MCLIENT,
	MM_EMULATE,
};

extern int startup_selection;

extern int fb_next_image(int offset);

extern void doexit(int sig);

extern int mplayer_disable;
extern int rfb_mode;
extern int flicker;
extern char *rtv_init_str;
extern char *mclient_server;

extern char cwd[];

extern void av_wss_update_aspect(av_wss_aspect_t aspect);
extern void av_wss_init();
extern void av_wss_visible(int isVisible);
extern void av_wss_redraw();

extern int wireless;

#endif /* MVPMC_H */
