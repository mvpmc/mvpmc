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

extern char *mythtv_server;
extern int mythtv_debug;

extern mvp_widget_t *file_browser;
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

extern char *current;
extern char *mythtv_recdir;

extern int fontid;
extern mvpw_screen_info_t si;

extern int running_mythtv;

extern void audio_play(mvp_widget_t*);
extern void video_play(mvp_widget_t*);

extern int mythtv_init(char*, int);
extern int gui_init(char*);

extern void audio_clear(void);
extern void video_clear(void);

extern int mythtv_back(mvp_widget_t*);
extern int fb_update(mvp_widget_t*);
extern int mythtv_update(mvp_widget_t*);

extern void video_callback(mvp_widget_t*, char);

extern void mythtv_show_widgets(void);

#endif /* MVPMC_H */
