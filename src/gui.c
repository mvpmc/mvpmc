/*
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

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_osd.h>

#include "mvpmc.h"

volatile int running_replaytv = 0;

static mvpw_menu_attr_t fb_attr = {
	.font = 0,
	.fg = MVPW_BLACK,
	.hilite_fg = MVPW_WHITE,
	.hilite_bg = MVPW_DARKGREY2,
	.title_fg = MVPW_WHITE,
	.title_bg = MVPW_BLUE,
};

static mvpw_menu_attr_t settings_attr = {
	.font = 0,
	.fg = MVPW_BLACK,
	.hilite_fg = MVPW_WHITE,
	.hilite_bg = MVPW_DARKGREY2,
	.title_fg = MVPW_WHITE,
	.title_bg = MVPW_BLUE,
};

static mvpw_menu_attr_t mythtv_attr = {
	.font = 0,
	.fg = MVPW_BLACK,
	.hilite_fg = MVPW_WHITE,
	.hilite_bg = MVPW_DARKGREY2,
	.title_fg = MVPW_WHITE,
	.title_bg = MVPW_BLUE,
};

static mvpw_menu_attr_t attr = {
	.font = 0,
	.fg = MVPW_BLACK,
	.hilite_fg = MVPW_BLACK,
	.hilite_bg = MVPW_WHITE,
	.title_fg = MVPW_WHITE,
	.title_bg = MVPW_BLUE,
};

static mvpw_menu_item_attr_t popup_item_attr = {
	.selectable = 1,
	.fg = mvpw_color_alpha(MVPW_BLACK, 0x80),
	.bg = mvpw_color_alpha(MVPW_DARK_ORANGE, 0x80),
	.checkbox_fg = mvpw_color_alpha(MVPW_PURPLE, 0x80),
};

static mvpw_menu_item_attr_t item_attr = {
	.selectable = 1,
	.fg = MVPW_WHITE,
	.bg = 0,
	.checkbox_fg = MVPW_GREEN,
};

static mvpw_menu_item_attr_t settings_item_attr = {
	.selectable = 1,
	.fg = MVPW_BLACK,
	.bg = MVPW_LIGHTGREY,
	.checkbox_fg = MVPW_GREEN,
};

static mvpw_menu_item_attr_t sub_settings_item_attr = {
	.selectable = 1,
	.fg = MVPW_BLACK,
	.bg = MVPW_LIGHTGREY,
	.checkbox_fg = MVPW_GREEN,
};

static mvpw_menu_attr_t popup_attr = {
	.font = 0,
	.fg = mvpw_color_alpha(MVPW_BLACK, 0x80),
	.hilite_fg = mvpw_color_alpha(MVPW_BLACK, 0x80),
	.hilite_bg = mvpw_color_alpha(MVPW_WHITE, 0x80),
	.title_fg = mvpw_color_alpha(MVPW_BLACK, 0x80),
	.title_bg = mvpw_color_alpha(MVPW_WHITE, 0x80),
};

static mvpw_text_attr_t about_attr = {
	.wrap = 1,
	.justify = MVPW_TEXT_LEFT,
	.margin = 4,
	.font = 0,
	.fg = MVPW_BLACK,
};

static mvpw_text_attr_t description_attr = {
	.wrap = 1,
	.justify = MVPW_TEXT_LEFT,
	.margin = 4,
	.font = 0,
	.fg = MVPW_WHITE,
};

static mvpw_text_attr_t display_attr = {
	.wrap = 0,
	.justify = MVPW_TEXT_LEFT,
	.margin = 6,
	.font = 0,
	.fg = MVPW_WHITE,
};

static mvpw_text_attr_t mythtv_program_attr = {
	.wrap = 0,
	.justify = MVPW_TEXT_LEFT,
	.margin = 6,
	.font = 0,
	.fg = MVPW_CYAN,
};

static mvpw_text_attr_t mythtv_description_attr = {
	.wrap = 1,
	.justify = MVPW_TEXT_LEFT,
	.margin = 6,
	.font = 0,
	.fg = MVPW_WHITE,
};

static mvpw_text_attr_t splash_attr = {
	.wrap = 1,
	.justify = MVPW_TEXT_LEFT,
	.margin = 6,
	.font = 0,
	.fg = MVPW_GREEN,
};

static mvpw_graph_attr_t offset_graph_attr = {
	.min = 0,
	.max = 100,
	.fg = mvpw_color_alpha(MVPW_RED, 0x80),
};

static mvpw_graph_attr_t demux_graph_attr = {
	.min = 0,
	.max = 1024*1024*2,
	.fg = mvpw_color_alpha(MVPW_BLUE, 0x80),
};

static int init_done = 0;

mvp_widget_t *root;
mvp_widget_t *iw;

static mvp_widget_t *splash;
static mvp_widget_t *main_menu;
static mvp_widget_t *mvpmc_logo;
static mvp_widget_t *settings;
static mvp_widget_t *sub_settings;
static mvp_widget_t *about;
static mvp_widget_t *setup_image;
static mvp_widget_t *fb_image;
static mvp_widget_t *mythtv_image;
static mvp_widget_t *replaytv_image;
static mvp_widget_t *about_image;
static mvp_widget_t *exit_image;

mvp_widget_t *file_browser;
mvp_widget_t *replaytv_browser;
mvp_widget_t *mythtv_browser;
mvp_widget_t *mythtv_logo;
mvp_widget_t *mythtv_date;
mvp_widget_t *mythtv_description;
mvp_widget_t *mythtv_channel;
mvp_widget_t *pause_widget;
mvp_widget_t *mute_widget;
mvp_widget_t *ffwd_widget;
mvp_widget_t *zoom_widget;
mvp_widget_t *osd_widget;
mvp_widget_t *offset_widget;
mvp_widget_t *offset_bar;
mvp_widget_t *bps_widget;
mvp_widget_t *time_widget;

mvp_widget_t *shows_widget;
mvp_widget_t *episodes_widget;

mvp_widget_t *popup_menu;
mvp_widget_t *audio_stream_menu;
mvp_widget_t *video_stream_menu;
mvp_widget_t *osd_menu;

mvp_widget_t *mythtv_program_widget;
mvp_widget_t *mythtv_osd_program;
mvp_widget_t *mythtv_osd_description;

mvp_widget_t *clock_widget;
mvp_widget_t *demux_video;
mvp_widget_t *demux_audio;

mvpw_screen_info_t si;

enum {
	MM_EXIT,
	MM_MYTHTV,
	MM_FILESYSTEM,
	MM_ABOUT,
	MM_SETTINGS,
	MM_REPLAYTV,
};

enum {
	SETTINGS_MODE,
	SETTINGS_OUTPUT,
	SETTINGS_FLICKER,
	SETTINGS_ASPECT,
};

enum {
	MENU_AUDIO_STREAM,
	MENU_VIDEO_STREAM,
	MENU_SUBTITLES,
	MENU_OSD,
};

osd_widget_t osd_widgets[MAX_OSD_WIDGETS];

void
add_osd_widget(mvp_widget_t *widget, int type, int visible,
	       void (*callback)(mvp_widget_t*))
{
	int i = 0;

	while (osd_widgets[i].widget != NULL)
		i++;

	osd_widgets[i].type = type;
	osd_widgets[i].visible = visible;
	osd_widgets[i].widget = widget;
	osd_widgets[i].callback = callback;
}

int
osd_widget_toggle(int type)
{
	int i = 0, on;
	void (*callback)(mvp_widget_t*);

	while ((osd_widgets[i].type != type) && (i < MAX_OSD_WIDGETS))
		i++;

	if (i == MAX_OSD_WIDGETS)
		return -1;

	on = osd_widgets[i].visible = !osd_widgets[i].visible;
	callback = osd_widgets[i].callback;

	if (callback) {
		if (on) {
			callback(osd_widgets[i].widget);
			mvpw_set_timer(osd_widgets[i].widget, callback, 1000);
			mvpw_show(osd_widgets[i].widget);
			mvpw_expose(osd_widgets[i].widget);
		} else {
			mvpw_set_timer(osd_widgets[i].widget, NULL, 0);
			mvpw_hide(osd_widgets[i].widget);
		}
	}

	return on;
}

static void settings_select_callback(mvp_widget_t*, char*, void*);
static void sub_settings_select_callback(mvp_widget_t*, char*, void*);
static void hide_widgets(void);

static int powered_off = 0;

void
power_toggle(void)
{
	static int on = 1;
	int afd, vfd;

	if (on == 0) {
		av_set_led(1);
		re_exec();
	}

	av_set_led(0);

	mvpw_focus(root);
	hide_widgets();
	mvpw_expose(root);

	video_clear();
	mvpw_set_idle(NULL);
	mvpw_set_timer(root, NULL, 0);

	afd = av_audio_fd();
	vfd = av_video_fd();

	close(afd);
	close(vfd);

	osd_destroy_all_surfaces();
	osd_close();

	on = 0;
	powered_off = 1;
}

static void
splash_update(void)
{
	char buf[128], *ptr;

	ptr = mvpw_get_text_str(splash);
	snprintf(buf, sizeof(buf), "%s.", ptr);

	mvpw_set_text_str(splash, buf);
	mvpw_expose(splash);
	mvpw_event_flush();
}

static void
hide_widgets(void)
{
	mvpw_hide(main_menu);
	mvpw_hide(mvpmc_logo);
	mvpw_hide(file_browser);
	mvpw_hide(settings);
	mvpw_hide(sub_settings);
	mvpw_hide(mythtv_browser);
	mvpw_hide(mythtv_logo);
	mvpw_hide(mythtv_channel);
	mvpw_hide(mythtv_date);
	mvpw_hide(mythtv_description);
	mvpw_hide(about);
	mvpw_hide(settings);
	mvpw_hide(sub_settings);
	mvpw_hide(iw);
	mvpw_hide(fb_image);
	mvpw_hide(mythtv_image);
	mvpw_hide(replaytv_image);
	mvpw_hide(setup_image);
	mvpw_hide(about_image);
	mvpw_hide(exit_image);
	mvpw_hide(fb_image);
	mvpw_hide(shows_widget);
	mvpw_hide(episodes_widget);
}

static void
root_callback(mvp_widget_t *widget, char key)
{
	if (powered_off) {
		if (key == 'P')
			power_toggle();
	} else {
		video_callback(widget, key);
	}
}

static void
main_menu_callback(mvp_widget_t *widget, char key)
{
	if (key == 'P')
		power_toggle();
}

static void
iw_key_callback(mvp_widget_t *widget, char key)
{
	mvpw_show(file_browser);
	mvpw_focus(file_browser);
}

static void
about_key_callback(mvp_widget_t *widget, char key)
{
	if (key == 'P') {
		power_toggle();
	} else {
		mvpw_hide(about);
		mvpw_show(root);
		mvpw_expose(root);
		mvpw_show(main_menu);
		mvpw_expose(main_menu);
		mvpw_focus(main_menu);
	}
}

static void
sub_settings_key_callback(mvp_widget_t *widget, char key)
{
	if (key == 'P')
		power_toggle();

	if (key == 'E') {
		mvpw_hide(settings);
		mvpw_hide(sub_settings);

		mvpw_show(main_menu);
		mvpw_show(mvpmc_logo);
		mvpw_show(setup_image);

		mvpw_focus(main_menu);
	}

	if (key == '<') {
		settings_attr.hilite_bg = MVPW_BLUE;
		mvpw_set_menu_attr(settings, &settings_attr);

		settings_attr.hilite_bg = MVPW_DARKGREY;
		mvpw_set_menu_attr(sub_settings, &settings_attr);

		mvpw_expose(settings);
		mvpw_expose(sub_settings);
		mvpw_focus(settings);
	}
}

static void
settings_key_callback(mvp_widget_t *widget, char key)
{
	if (key == 'P')
		power_toggle();

	if (key == 'E') {
		mvpw_hide(settings);
		mvpw_hide(sub_settings);

		mvpw_show(main_menu);
		mvpw_show(mvpmc_logo);
		mvpw_show(setup_image);

		mvpw_focus(main_menu);
	}

	if (key == '>') {
		settings_select_callback(NULL, NULL, NULL);
	}
}

static void
replaytv_key_callback(mvp_widget_t *widget, char key)
{
	if (key == 'P')
		power_toggle();

	if (key == 'E') {
		replaytv_stop();

		mvpw_hide(replaytv_browser);

		mvpw_show(main_menu);
		mvpw_show(mvpmc_logo);
		mvpw_show(replaytv_image);

		mvpw_focus(main_menu);
	}
}

static void
fb_key_callback(mvp_widget_t *widget, char key)
{
	if (key == 'P')
		power_toggle();

	if (key == 'E') {
		mvpw_hide(widget);
		mvpw_hide(iw);

		mvpw_show(main_menu);
		mvpw_show(mvpmc_logo);
		mvpw_show(fb_image);
		mvpw_focus(main_menu);

		mvpw_set_idle(NULL);
		mvpw_set_timer(root, NULL, 0);

		audio_clear();
		video_clear();
	}
}

static void
mythtv_key_callback(mvp_widget_t *widget, char key)
{
	if (key == 'P')
		power_toggle();

	if (key == 'E') {
		if (mythtv_back(widget) == 0) {
			mvpw_hide(mythtv_browser);
			mvpw_hide(mythtv_logo);
			mvpw_hide(mythtv_channel);
			mvpw_hide(mythtv_date);
			mvpw_hide(mythtv_description);
			mvpw_hide(shows_widget);
			mvpw_hide(episodes_widget);

			mvpw_show(mythtv_image);
			mvpw_show(main_menu);
			mvpw_show(mvpmc_logo);
			mvpw_focus(main_menu);
		}
	}
}

static void
popup_key_callback(mvp_widget_t *widget, char key)
{
	if (key == 'P')
		power_toggle();

	if (key == 'M') {
		mvpw_hide(popup_menu);
		mvpw_hide(audio_stream_menu);
		mvpw_hide(video_stream_menu);
		mvpw_hide(osd_menu);
		mvpw_focus(root);
	}

	if (key == 'E') {
		if (mvpw_visible(popup_menu)) {
			mvpw_hide(popup_menu);
			mvpw_focus(root);
		} else {
			mvpw_hide(audio_stream_menu);
			mvpw_hide(video_stream_menu);
			mvpw_hide(osd_menu);
			mvpw_show(popup_menu);
			mvpw_focus(popup_menu);
		}
	}
}

static int
file_browser_init(void)
{
	file_browser = mvpw_create_menu(NULL, 50, 30, si.cols-120, si.rows-190,
					0xff808080, 0xff606060, 2);

	fb_attr.font = fontid;
	mvpw_set_menu_attr(file_browser, &fb_attr);

	mvpw_set_menu_title(file_browser, "/");
	mvpw_set_bg(file_browser, MVPW_LIGHTGREY);

	mvpw_set_key(file_browser, fb_key_callback);

	splash_update();

	return 0;
}

static void
audio_stream_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	audio_switch_stream(widget, (int)key);
}

static void
video_stream_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	video_switch_stream(widget, (int)key);
}

static void
osd_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	osd_type_t type = (osd_type_t)key;
	int on;

	printf("OSD callback on '%s' %d\n", item, (int)key);

	on = osd_widget_toggle(type);

	mvpw_check_menu_item(osd_menu, (void*)key, on);
	mvpw_expose(osd_menu);
}

static void
popup_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	mvpw_hide(popup_menu);

	switch ((int)key) {
	case MENU_AUDIO_STREAM:
		popup_item_attr.select = audio_stream_select_callback;
		add_audio_streams(audio_stream_menu, &popup_item_attr);
		mvpw_show(audio_stream_menu);
		mvpw_focus(audio_stream_menu);
		break;
	case MENU_VIDEO_STREAM:
		popup_item_attr.select = video_stream_select_callback;
		add_video_streams(video_stream_menu, &popup_item_attr);
		mvpw_show(video_stream_menu);
		mvpw_focus(video_stream_menu);
		break;
	case MENU_SUBTITLES:
		break;
	case MENU_OSD:
		mvpw_show(osd_menu);
		mvpw_focus(osd_menu);
		break;
	case 4:
		break;
	}
}

static void
sub_settings_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	if ((strcmp(item, "PAL") == 0) || (strcmp(item, "NTSC") == 0)) {
		if (av_set_mode((int)key) < 0)
			printf("set mode to %s failed\n", item);
		else
			printf("set mode to %s\n", item);
	}

	if ((strcmp(item, "Composite") == 0) || (strcmp(item, "S-Video") == 0)) {
		if (av_set_output((int)key) < 0)
			printf("set output to %s failed\n", item);
		else
			printf("set output to %s\n", item);
	}

	if ((strcmp(item, "4:3") == 0) || (strcmp(item, "16:9") == 0)) {
		if (av_set_aspect((int)key) < 0)
			printf("set aspect to %s failed\n", item);
		else
			printf("set aspect to %s\n", item);
	}
}

static void
sub_settings_hilite_callback(mvp_widget_t *widget, char *item, void *key,
			     int hilite)
{
}

static void
settings_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	settings_attr.hilite_bg = MVPW_BLUE;
	mvpw_set_menu_attr(sub_settings, &settings_attr);

	settings_attr.hilite_bg = MVPW_DARKGREY;
	mvpw_set_menu_attr(settings, &settings_attr);

	mvpw_expose(settings);
	mvpw_expose(sub_settings);

	mvpw_focus(sub_settings);
}

static void
settings_hilite_callback(mvp_widget_t *widget, char *item, void *key,
			 int hilite)
{
	if (hilite) {
		mvpw_clear_menu(sub_settings);
		sub_settings_item_attr.hilite = sub_settings_hilite_callback;
		sub_settings_item_attr.select = sub_settings_select_callback;
		switch ((int)key) {
		case SETTINGS_MODE:
			mvpw_add_menu_item(sub_settings, "PAL",
					   (void*)AV_MODE_PAL,
					   &sub_settings_item_attr);
			mvpw_add_menu_item(sub_settings, "NTSC",
					   (void*)AV_MODE_NTSC,
					   &sub_settings_item_attr);
			mvpw_menu_hilite_item(sub_settings,
					      (void*)av_get_mode());
			break;
		case SETTINGS_OUTPUT:
			mvpw_add_menu_item(sub_settings, "Composite",
					   (void*)AV_OUTPUT_COMPOSITE,
					   &sub_settings_item_attr);
			mvpw_add_menu_item(sub_settings, "S-Video",
					   (void*)AV_OUTPUT_SVIDEO,
					   &sub_settings_item_attr);
			mvpw_menu_hilite_item(sub_settings,
					      (void*)av_get_output());
			break;
		case SETTINGS_FLICKER:
			break;
		case SETTINGS_ASPECT:
			mvpw_add_menu_item(sub_settings, "4:3",
					   (void*)AV_ASPECT_4x3,
					   &sub_settings_item_attr);
			mvpw_add_menu_item(sub_settings, "16:9",
					   (void*)AV_ASPECT_16x9,
					   &sub_settings_item_attr);
			mvpw_menu_hilite_item(sub_settings,
					      (void*)av_get_aspect());
			break;
		}
	} else {
	}
}

static int
settings_init(void)
{
	int x, y, w, h;

	settings_attr.font = fontid;
	h = mvpw_font_height(description_attr.font) +
		(2 * description_attr.margin);
	w = (si.cols - 100) / 2;

	x = 50;
	y = (si.rows / 2) - (h * 2);

	settings = mvpw_create_menu(NULL, x, y, w, h*4,
				    0xff808080, 0xff606060, 2);

	settings_attr.hilite_bg = MVPW_BLUE;
	mvpw_set_key(settings, settings_key_callback);
	mvpw_set_menu_attr(settings, &settings_attr);

	mvpw_set_bg(settings, MVPW_LIGHTGREY);

	settings_attr.hilite_bg = MVPW_DARKGREY;
	x += w;
	sub_settings = mvpw_create_menu(NULL, x, y, w, h*4,
					0xff808080, 0xff606060, 2);
	mvpw_set_key(sub_settings, sub_settings_key_callback);
	mvpw_set_menu_attr(sub_settings, &settings_attr);
	mvpw_set_bg(sub_settings, MVPW_LIGHTGREY);

	mvpw_attach(settings, sub_settings, MVPW_DIR_RIGHT);

	settings_item_attr.hilite = settings_hilite_callback;
	settings_item_attr.select = settings_select_callback;
	mvpw_add_menu_item(settings, "TV Mode",
			   (void*)SETTINGS_MODE, &settings_item_attr);
	mvpw_add_menu_item(settings, "Output",
			   (void*)SETTINGS_OUTPUT, &settings_item_attr);
#if 0
	mvpw_add_menu_item(settings, "Flicker Control",
			   (void*)SETTINGS_FLICKER, &settings_item_attr);
#endif
	mvpw_add_menu_item(settings, "Aspect Ratio",
			   (void*)SETTINGS_ASPECT, &settings_item_attr);

	sub_settings_item_attr.hilite = sub_settings_hilite_callback;
	sub_settings_item_attr.select = sub_settings_select_callback;

	splash_update();

	return 0;
}

static int
myth_browser_init(void)
{
	mvpw_image_info_t iid;
	mvpw_widget_info_t wid, wid2, info;
	int h;
	char file[128];

	snprintf(file, sizeof(file), "%s/mythtv_logo_rotate.png", imagedir);
	if (mvpw_get_image_info(file, &iid) < 0)
		return -1;
	mythtv_logo = mvpw_create_image(NULL, 50, 25, iid.width, iid.height,
				       0, 0, 0);
	mvpw_set_image(mythtv_logo, file);

	mythtv_browser = mvpw_create_menu(NULL, 50, 30,
					  si.cols-120-iid.width, si.rows-190,
					  0xff808080, 0xff606060, 2);

	mvpw_attach(mythtv_logo, mythtv_browser, MVPW_DIR_RIGHT);

	mvpw_set_key(mythtv_browser, mythtv_key_callback);

	mythtv_attr.font = fontid;
	mvpw_set_menu_attr(mythtv_browser, &mythtv_attr);

	description_attr.font = fontid;
	h = mvpw_font_height(description_attr.font) +
		(2 * description_attr.margin);

	mythtv_channel = mvpw_create_text(NULL, 0, 0, 350, h,
					  MVPW_BLACK, 0, 0);
	mythtv_date = mvpw_create_text(NULL, 0, 0, 350, h,
				       MVPW_BLACK, 0, 0);
	mythtv_description = mvpw_create_text(NULL, 0, 0, 350, h*3,
					      MVPW_BLACK, 0, 0);

	mvpw_set_text_attr(mythtv_channel, &description_attr);
	mvpw_set_text_attr(mythtv_date, &description_attr);
	mvpw_set_text_attr(mythtv_description, &description_attr);

	mvpw_get_widget_info(mythtv_logo, &wid);
	mvpw_get_widget_info(mythtv_browser, &wid2);
	mvpw_moveto(mythtv_channel, wid.x, wid2.y+wid2.h);
	mvpw_get_widget_info(mythtv_channel, &wid2);
	mvpw_moveto(mythtv_date, wid.x, wid2.y+wid2.h);
	mvpw_get_widget_info(mythtv_date, &wid2);
	mvpw_moveto(mythtv_description, wid.x, wid2.y+wid2.h);

	/*
	 * MythTV menu info
	 */
	mvpw_get_widget_info(mythtv_channel, &info);
	shows_widget = mvpw_create_text(NULL, info.x, info.y,
					300, h, 0x80000000, 0, 0);
	episodes_widget = mvpw_create_text(NULL, 50, 80,
					   300, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(shows_widget, &description_attr);
	mvpw_set_text_attr(episodes_widget, &description_attr);

	mvpw_attach(shows_widget, episodes_widget, MVPW_DIR_DOWN);

	mvpw_raise(mythtv_browser);

	splash_update();

	return 0;
}

static int
replaytv_browser_init(void)
{
	replaytv_browser = mvpw_create_menu(NULL, 50, 30,
					    si.cols-120, si.rows-190,
					    0xff808080, 0xff606060, 2);

	fb_attr.font = fontid;
	mvpw_set_menu_attr(replaytv_browser, &fb_attr);

	mvpw_set_menu_title(replaytv_browser, "ReplayTV");
	mvpw_set_bg(replaytv_browser, MVPW_LIGHTGREY);

	mvpw_set_key(replaytv_browser, replaytv_key_callback);

	splash_update();

	return 0;
}

static void
main_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	int k = (int)key;

	switch (k) {
	case MM_EXIT:
		exit(0);
		break;
	case MM_FILESYSTEM:
		mvpw_hide(main_menu);
		mvpw_hide(mvpmc_logo);
		mvpw_hide(fb_image);

		fb_update(file_browser);

		mvpw_show(file_browser);
		mvpw_focus(file_browser);
		break;
	case MM_SETTINGS:
		mvpw_hide(main_menu);
		mvpw_hide(setup_image);

		settings_attr.hilite_bg = MVPW_BLUE;
		mvpw_set_menu_attr(settings, &settings_attr);

		settings_attr.hilite_bg = MVPW_DARKGREY;
		mvpw_set_menu_attr(sub_settings, &settings_attr);

		mvpw_show(settings);
		mvpw_show(sub_settings);
		mvpw_focus(settings);
		break;
	case MM_MYTHTV:
		mvpw_hide(main_menu);
		mvpw_hide(mvpmc_logo);
		mvpw_hide(mythtv_image);

		mythtv_update(mythtv_browser);

		mvpw_show(mythtv_logo);
		mvpw_show(mythtv_browser);
		break;
	case MM_REPLAYTV:
		mvpw_hide(main_menu);
		mvpw_hide(mvpmc_logo);
		mvpw_hide(replaytv_image);
		mvpw_hide(fb_image);

		replaytv_update(replaytv_browser);

		mvpw_show(replaytv_browser);
		break;
	case MM_ABOUT:
		mvpw_show(about);
		mvpw_focus(about);
		break;
	}
}

static void
main_hilite_callback(mvp_widget_t *widget, char *item, void *key, int hilite)
{
	int k = (int)key;

	if (!init_done)
		return;

	if (hilite) {
		switch (k) {
		case MM_SETTINGS:
			mvpw_show(setup_image);
			break;
		case MM_FILESYSTEM:
			mvpw_show(fb_image);
			break;
		case MM_MYTHTV:
			mvpw_show(mythtv_image);
			break;
		case MM_REPLAYTV:
			mvpw_show(replaytv_image);
			break;
		case MM_ABOUT:
			mvpw_show(about_image);
			break;
		case MM_EXIT:
			mvpw_show(exit_image);
			break;
		}
	} else {
		switch (k) {
		case MM_SETTINGS:
			mvpw_hide(setup_image);
			break;
		case MM_FILESYSTEM:
			mvpw_hide(fb_image);
			break;
		case MM_MYTHTV:
			mvpw_hide(mythtv_image);
			break;
		case MM_REPLAYTV:
			mvpw_hide(replaytv_image);
			break;
		case MM_ABOUT:
			mvpw_hide(about_image);
			break;
		case MM_EXIT:
			mvpw_hide(exit_image);
			break;
		}
	}
}

int
main_menu_init(char *server, char *replaytv)
{
	mvpw_image_info_t iid;
	mvpw_widget_info_t wid;
	char file[128];

	snprintf(file, sizeof(file), "%s/wrench.png", imagedir);
	if (mvpw_get_image_info(file, &iid) < 0)
		return -1;
	setup_image = mvpw_create_image(NULL, 50, 25,
					iid.width, iid.height, 0, 0, 0);
	mvpw_set_image(setup_image, file);
	splash_update();

	snprintf(file, sizeof(file), "%s/video_folder.png", imagedir);
	if (mvpw_get_image_info(file, &iid) < 0)
		return -1;
	fb_image = mvpw_create_image(NULL, 50, 25,
				     iid.width, iid.height, 0, 0, 0);
	mvpw_set_image(fb_image, file);
	splash_update();

	snprintf(file, sizeof(file), "%s/tv2.png", imagedir);
	if (mvpw_get_image_info(file, &iid) < 0)
		return -1;
	mythtv_image = mvpw_create_image(NULL, 50, 25,
					 iid.width, iid.height, 0, 0, 0);
	mvpw_set_image(mythtv_image, file);
	splash_update();

	snprintf(file, sizeof(file), "%s/replaytv1.png", imagedir);
	if (mvpw_get_image_info(file, &iid) < 0)
		return -1;
	replaytv_image = mvpw_create_image(NULL, 50, 25,
					iid.width, iid.height, 0, 0, 0);
	mvpw_set_image(replaytv_image, file);
	splash_update();

	snprintf(file, sizeof(file), "%s/unknown.png", imagedir);
	if (mvpw_get_image_info(file, &iid) < 0)
		return -1;
	about_image = mvpw_create_image(NULL, 50, 25,
					iid.width, iid.height, 0, 0, 0);
	mvpw_set_image(about_image, file);
	splash_update();

	snprintf(file, sizeof(file), "%s/stop.png", imagedir);
	if (mvpw_get_image_info(file, &iid) < 0)
		return -1;
	exit_image = mvpw_create_image(NULL, 50, 25,
				       iid.width, iid.height, 0, 0, 0);
	mvpw_set_image(exit_image, file);
	splash_update();

	snprintf(file, sizeof(file), "%s/mvpmc_logo.png", imagedir);
	if (mvpw_get_image_info(file, &iid) < 0)
		return -1;
	mvpmc_logo = mvpw_create_image(NULL, 50, 25, iid.width, iid.height,
				       0, 0, 0);
	mvpw_set_image(mvpmc_logo, file);
	splash_update();

	main_menu = mvpw_create_menu(NULL, 50, 50, iid.width, si.rows-150,
				     0, 0, 0);

	mvpw_attach(mvpmc_logo, main_menu, MVPW_DIR_DOWN);
	mvpw_attach(main_menu, setup_image, MVPW_DIR_RIGHT);

	mvpw_get_widget_info(setup_image, &wid);
	mvpw_moveto(fb_image, wid.x, wid.y);
	mvpw_moveto(mythtv_image, wid.x, wid.y);
	mvpw_moveto(replaytv_image, wid.x, wid.y);
	mvpw_moveto(about_image, wid.x, wid.y);
	mvpw_moveto(exit_image, wid.x, wid.y);

	attr.font = fontid;
	mvpw_set_menu_attr(main_menu, &attr);

	item_attr.select = main_select_callback;
	item_attr.hilite = main_hilite_callback;

	if (server)
		mvpw_add_menu_item(main_menu, "MythTV",
				   (void*)MM_MYTHTV, &item_attr);
	if (replaytv)
		mvpw_add_menu_item(main_menu, "ReplayTV",
				   (void*)MM_REPLAYTV, &item_attr);
	mvpw_add_menu_item(main_menu, "Filesystem",
			   (void*)MM_FILESYSTEM, &item_attr);
	mvpw_add_menu_item(main_menu, "Settings",
			   (void*)MM_SETTINGS, &item_attr);
	mvpw_add_menu_item(main_menu, "About",
			   (void*)MM_ABOUT, &item_attr);
	mvpw_add_menu_item(main_menu, "Exit",
			   (void*)MM_EXIT, &item_attr);

	mvpw_set_key(main_menu, main_menu_callback);

	splash_update();

	return 0;
}

static int
about_init(void)
{
	int x, y, w, h;

	x = 150;
	y = 150;
	w = si.cols - (x * 2);
	h = si.rows - (y * 2);

	about = mvpw_create_text(NULL, x, y, w, h,
				 MVPW_LIGHTGREY, MVPW_DARKGREY, 2);

	mvpw_set_text_str(about,
			  "MediaMVP Media Center\n"
			  "http://mvpmc.sourceforge.net/\n\n"
			  "Audio: mp3, wav, ac3\n"
			  "Video: mpeg1, mpeg2\n"
			  "Images: bmp, gif, png, jpeg\n"
			  "Servers: MythTV, ReplayTV, NFS\n");
	mvpw_set_key(about, about_key_callback);

	about_attr.font = fontid;
	mvpw_set_text_attr(about, &about_attr);

	splash_update();

	return 0;
}

static int
image_init(void)
{
	iw = mvpw_create_image(NULL, 0, 0, si.cols, si.rows, 0, 0, 0);
	mvpw_set_key(iw, iw_key_callback);

	splash_update();

	return 0;
}

static int
osd_init(void)
{
	mvp_widget_t *widget, *contain, *progress;
	int h, w, x, y;

	display_attr.font = fontid;
	h = mvpw_font_height(display_attr.font) +
		(2 * display_attr.margin);
	w = mvpw_font_width(display_attr.font, " 000% ");

	/*
	 * State widgets for pause, mute, fast forward, zoom
	 */
	mute_widget = mvpw_create_text(NULL, 50, 25, 75, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(mute_widget, &display_attr);
	mvpw_set_text_str(mute_widget, "MUTE");

	pause_widget = mvpw_create_text(NULL, 50, 25, 75, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(pause_widget, &display_attr);
	mvpw_set_text_str(pause_widget, "PAUSE");

	ffwd_widget = mvpw_create_text(NULL, 50, 25, 75, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(ffwd_widget, &display_attr);
	mvpw_set_text_str(ffwd_widget, "FFWD");

	zoom_widget = mvpw_create_text(NULL, 50, 25, 75, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(zoom_widget, &display_attr);
	mvpw_set_text_str(zoom_widget, "ZOOM");

	clock_widget = mvpw_create_text(NULL, 50, 25, 150, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(clock_widget, &display_attr);
	mvpw_set_text_str(clock_widget, "");

	mvpw_attach(mute_widget, pause_widget, MVPW_DIR_RIGHT);
	mvpw_attach(pause_widget, ffwd_widget, MVPW_DIR_RIGHT);
	mvpw_attach(ffwd_widget, zoom_widget, MVPW_DIR_RIGHT);
	mvpw_attach(zoom_widget, clock_widget, MVPW_DIR_RIGHT);

	/*
	 * OSD widgets
	 */
	contain = mvpw_create_container(NULL, 50, 80,
					300, h, 0x80000000, 0, 0);
	progress = contain;
	widget = mvpw_create_text(contain, 0, 0, w, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(widget, &display_attr);
	mvpw_set_text_str(widget, "0%");
	mvpw_show(widget);
	offset_widget = widget;
	widget = mvpw_create_graph(contain, w, 0, 300-w, h/2,
				   0x80000000, 0, 0);
	mvpw_set_graph_attr(widget, &offset_graph_attr);
	mvpw_show(widget);
	offset_bar = widget;
	add_osd_widget(contain, OSD_PROGRESS, 1, NULL);

	mvpw_attach(mute_widget, contain, MVPW_DIR_DOWN);
	mvpw_set_text_attr(mute_widget, &display_attr);
	mvpw_show(widget);

	time_widget = mvpw_create_text(NULL, 0, 0, 150, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(time_widget, &display_attr);
	mvpw_set_text_str(time_widget, "");
	mvpw_attach(contain, time_widget, MVPW_DIR_DOWN);
	add_osd_widget(time_widget, OSD_TIMECODE, 1, NULL);

	bps_widget = mvpw_create_text(NULL, 0, 0, 150, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(bps_widget, &display_attr);
	mvpw_set_text_str(bps_widget, "");
	mvpw_attach(time_widget, bps_widget, MVPW_DIR_RIGHT);
	add_osd_widget(bps_widget, OSD_BITRATE, 1, NULL);

	/*
	 * myth OSD
	 */
	mythtv_program_attr.font = fontid;
	mythtv_description_attr.font = fontid;
	x = si.cols - 475;
	y = si.rows - 125;
	contain = mvpw_create_container(NULL, x, y,
					400, h*3, 0x80000000, 0, 0);
	mythtv_program_widget = contain;
	widget = mvpw_create_text(contain, 0, 0, 400, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(widget, &mythtv_program_attr);
	mvpw_set_text_str(widget, "");
	mvpw_show(widget);
	mythtv_osd_program = widget;
	widget = mvpw_create_text(contain, 0, 0, 400, h*2, 0x80000000, 0, 0);
	mvpw_set_text_attr(widget, &mythtv_description_attr);
	mvpw_set_text_str(widget, "");
	mvpw_show(widget);
	mythtv_osd_description = widget;
	mvpw_attach(mythtv_osd_program, mythtv_osd_description, MVPW_DIR_DOWN);

	x = si.cols - 475;
	y = si.rows - 125;
	contain = mvpw_create_container(NULL, x, y,
					300, h*2, 0x80000000, 0, 0);
	mvpw_attach(progress, contain, MVPW_DIR_RIGHT);
	widget = mvpw_create_graph(contain, 0, 0, 300, h,
				   0x80000000, 0, 0);
	mvpw_set_graph_attr(widget, &demux_graph_attr);
	mvpw_show(widget);
	demux_video = widget;
	widget = mvpw_create_graph(contain, 0, 0, 300, h,
				   0x80000000, 0, 0);
	mvpw_set_graph_attr(widget, &demux_graph_attr);
	mvpw_show(widget);
	demux_audio = widget;
	mvpw_attach(demux_video, demux_audio, MVPW_DIR_DOWN);
	add_osd_widget(contain, OSD_DEMUX, 0, NULL);

	add_osd_widget(clock_widget, OSD_CLOCK, 0, NULL);

	splash_update();

	return 0;
}

int
mw_init(char *server, char *replaytv)
{
	int h, w, x, y;
	char buf[128];

	mvpw_init();
	root = mvpw_get_root();
	mvpw_set_key(root, root_callback);

	mvpw_get_screen_info(&si);

	printf("screen is %d x %d\n", si.cols, si.rows);

	snprintf(buf, sizeof(buf), "Connecting to mythtv server %s", server);

	splash_attr.font = fontid;
	h = (mvpw_font_height(splash_attr.font) +
	     (2 * splash_attr.margin)) * 2;
	w = mvpw_font_width(fontid, buf) + 8;

	x = (si.cols - w) / 2;
	y = (si.rows - h) / 2;

	splash = mvpw_create_text(NULL, x, y, w, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(splash, &splash_attr);

	if (server)
		mvpw_set_text_str(splash, buf);

	mvpw_show(splash);
	mvpw_event_flush();

	return 0;
}

int
popup_init(void)
{
	int x, y, w, h;
	unsigned int bg;

	w = 300;
	h = 150;
	x = (si.cols - w) / 2;
	y = (si.rows - h) / 2;

	bg = mvpw_color_alpha(MVPW_DARK_ORANGE, 0x80);
	popup_menu = mvpw_create_menu(NULL, x, y, w, h,
				      bg,
				      mvpw_color_alpha(MVPW_BLACK, 0x80), 2);

	popup_attr.font = fontid;
	mvpw_set_menu_attr(popup_menu, &popup_attr);

	mvpw_set_menu_title(popup_menu, "Settings");
	mvpw_set_bg(popup_menu, bg);

	popup_item_attr.select = popup_select_callback;

	mvpw_add_menu_item(popup_menu, "Audio Streams",
			   (void*)MENU_AUDIO_STREAM, &popup_item_attr);
	mvpw_add_menu_item(popup_menu, "Video Streams",
			   (void*)MENU_VIDEO_STREAM, &popup_item_attr);
#if 0
	mvpw_add_menu_item(popup_menu, "Subtitles",
			   (void*)MENU_SUBTITLES, &popup_item_attr);
#endif
	mvpw_add_menu_item(popup_menu, "On Screen Display",
			   (void*)MENU_OSD, &popup_item_attr);

	mvpw_set_key(popup_menu, popup_key_callback);

	/*
	 * audio stream
	 */
	popup_attr.checkboxes = 1;
	audio_stream_menu = mvpw_create_menu(NULL, x, y, w, h,
					     bg,mvpw_color_alpha(MVPW_BLACK, 0x80), 2);
	mvpw_set_menu_attr(audio_stream_menu, &popup_attr);
	mvpw_set_menu_title(audio_stream_menu, "Audio Streams");
	mvpw_set_bg(audio_stream_menu, bg);
	mvpw_set_key(audio_stream_menu, popup_key_callback);

	/*
	 * video menu
	 */
	video_stream_menu = mvpw_create_menu(NULL, x, y, w, h,
					     bg,mvpw_color_alpha(MVPW_BLACK, 0x80), 2);
	mvpw_set_menu_attr(video_stream_menu, &popup_attr);
	mvpw_set_menu_title(video_stream_menu, "Video Streams");
	mvpw_set_bg(video_stream_menu, bg);
	mvpw_set_key(video_stream_menu, popup_key_callback);

	/*
	 * osd menu
	 */
	osd_menu = mvpw_create_menu(NULL, x, y, w, h,
				    bg,mvpw_color_alpha(MVPW_BLACK, 0x80), 2);
	mvpw_set_menu_attr(osd_menu, &popup_attr);
	mvpw_set_menu_title(osd_menu, "OSD Settings");
	mvpw_set_bg(osd_menu, bg);
	mvpw_set_key(osd_menu, popup_key_callback);

	/*
	 * osd sub-menu
	 */
	popup_item_attr.select = osd_select_callback;
	mvpw_add_menu_item(osd_menu, "Bitrate", (void*)OSD_BITRATE,
			   &popup_item_attr);
	mvpw_add_menu_item(osd_menu, "Clock", (void*)OSD_CLOCK,
			   &popup_item_attr);
	mvpw_add_menu_item(osd_menu, "Demux Info", (void*)OSD_DEMUX,
			   &popup_item_attr);
	mvpw_add_menu_item(osd_menu, "Progress Meter", (void*)OSD_PROGRESS,
			   &popup_item_attr);
	mvpw_add_menu_item(osd_menu, "Program Info", (void*)OSD_PROGRAM,
			   &popup_item_attr);
	mvpw_add_menu_item(osd_menu, "Timecode", (void*)OSD_TIMECODE,
			   &popup_item_attr);

	mvpw_check_menu_item(osd_menu, (void*)OSD_BITRATE, 1);
	mvpw_check_menu_item(osd_menu, (void*)OSD_CLOCK, 0);
	mvpw_check_menu_item(osd_menu, (void*)OSD_DEMUX, 0);
	mvpw_check_menu_item(osd_menu, (void*)OSD_PROGRESS, 1);
	mvpw_check_menu_item(osd_menu, (void*)OSD_PROGRAM, 1);
	mvpw_check_menu_item(osd_menu, (void*)OSD_TIMECODE, 1);

	return 0;
}

int
gui_init(char *server, char *replaytv)
{
	char buf[128], *ptr;

	ptr = mvpw_get_text_str(splash);
	if (ptr)
		snprintf(buf, sizeof(buf), "%s\nInitializing GUI", ptr);
	else
		snprintf(buf, sizeof(buf), "Initializing GUI");

	mvpw_set_text_str(splash, buf);
	mvpw_expose(splash);
	mvpw_event_flush();

	if (main_menu_init(server, replaytv) < 0)
		return -1;
	if (myth_browser_init() < 0)
		return -1;
	replaytv_browser_init();
	file_browser_init();
	settings_init();
	about_init();
	image_init();
	osd_init();
	popup_init();

	mvpw_destroy(splash);

	init_done = 1;

	if (server)
		mvpw_show(mythtv_image);
	else if (replaytv)
		mvpw_show(replaytv_image);
	else
		mvpw_show(fb_image);
	mvpw_show(mvpmc_logo);
	mvpw_show(main_menu);

	mvpw_focus(main_menu);

	return 0;
}
