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

typedef enum {
	OSD_BITRATE = 1,
	OSD_CLOCK,
	OSD_DEMUX,
	OSD_PROGRESS,
	OSD_PROGRAM,
	OSD_TIMECODE,
} osd_type_t;

typedef struct {
	int type;
	int visible;
	mvp_widget_t *widget;
	void (*callback)(mvp_widget_t *widget);
} osd_widget_t;

extern char *mythtv_server;
extern int mythtv_debug;

extern mvp_widget_t *file_browser;
extern mvp_widget_t *replaytv_browser;
extern mvp_widget_t *mythtv_browser;
extern mvp_widget_t *mythtv_logo;
extern mvp_widget_t *mythtv_date;
extern mvp_widget_t *mythtv_description;
extern mvp_widget_t *mythtv_channel;
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

extern mvp_widget_t *episodes_widget;
extern mvp_widget_t *shows_widget;

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

extern char *replaytv_server;

extern int fontid;
extern mvpw_screen_info_t si;

extern int running_mythtv;
extern int running_replaytv;

extern int fd_audio, fd_video;

extern void audio_play(mvp_widget_t*);
extern void video_play(mvp_widget_t*);

extern int mythtv_init(char*, int);
extern int gui_init(char*, char*);

extern void audio_clear(void);
extern void video_clear(void);

extern int mythtv_back(mvp_widget_t*);
extern int fb_update(mvp_widget_t*);
extern int mythtv_update(mvp_widget_t*);

extern void video_callback(mvp_widget_t*, char);

extern void mythtv_show_widgets(void);
extern void mythtv_program(mvp_widget_t *widget);

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

#endif /* MVPMC_H */
