/*
 *  Copyright (C) 2006, Sergio Slobodrian
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <mvp_demux.h>
#include <mvp_widget.h>
#include <mvp_av.h>
#include <ts_demux.h>

#include "mvpmc.h"
#include "mythtv.h"
#include "replaytv.h"
#include "config.h"

#if 0
#define PRINTF(x...) printf(x)
#define TRC(fmt, args...) printf(fmt, ## args) 
#else
#define PRINTF(x...)
#define TRC(fmt, args...) 
#endif

#ifdef MVPMC_HOST
#define FONT_STANDARD	0
#define FONT_LARGE	0
#else
#define FONT_STANDARD	1000
#define FONT_LARGE	1001
#endif

#define FONT_HEIGHT(x)	(mvpw_font_height(x.font,x.utf8) + (2 * x.margin))
#define FONT_WIDTH(x,c)	(mvpw_font_width(x.font,c,x.utf8))

extern int showing_guide;
cmyth_chanlist_t tvguide_chanlist = NULL;
cmyth_tvguide_progs_t tvguide_proglist = NULL;
int tvguide_cur_chan_index;
int tvguide_scroll_ofs_x = 0;
int tvguide_scroll_ofs_y = 0;
long tvguide_free_cardids = 0;
int mythtv_tvguide_sort_desc = 0;
static int tvguide_popup_menu = 0;

typedef struct {
	char keys[4];
	int key_ct;
} key_set;


/*
 * livetv guide attributes
 */

/* Clock window */
static mvpw_text_attr_t livetv_header_attr = {
	.wrap = 1,
	.pack = 1,
	.justify = MVPW_TEXT_CENTER,
	.margin = 9,
	.font = FONT_LARGE,
	.fg = MVPW_WHITE,
	.bg = MVPW_RGBA(25,112,25,255),
	.border = MVPW_BLACK,
	.border_size = 0,
};

/* Description window */
static mvpw_text_attr_t livetv_description_attr = {
	.wrap = true,
	.pack = true,
	.justify = MVPW_TEXT_LEFT,
	.margin = 9,
	.font = FONT_LARGE,
	.fg = MVPW_WHITE,
	.bg = MVPW_DARKGREY,
	.border = MVPW_BLACK,
	.border_size = 0,
};

/* TV guide array widget */
static mvpw_array_attr_t livetv_program_list_attr = {
	.rows = 4,
	.cols = 3,
	.col_label_height = 29,
	.row_label_width = 130,
	.array_border = 0,
	.border_size = 0,
	.row_label_fg = MVPW_WHITE,
	.row_label_bg = MVPW_DARKGREY,
	.col_label_fg = MVPW_WHITE,
	.col_label_bg = MVPW_DARK_GREEN,
	.cell_fg = MVPW_WHITE,
	.cell_bg = MVPW_MIDNIGHTBLUE,
	.hilite_fg = MVPW_BLACK,
	.hilite_bg = MVPW_ALMOSTWHITEGREY,
	.cell_rounded = 0,
};

/* The popup menu for scheduling auto tune and programs */
mvpw_menu_attr_t tvguide_popup_attr = {
	.font = FONT_LARGE,
	.fg = MVPW_WHITE,
	.bg = MVPW_LIGHTGREY,
	.hilite_fg = MVPW_WHITE,
	.hilite_bg = MVPW_DARK_GREEN,
	.title_fg = MVPW_WHITE,
	.title_bg = MVPW_MIDNIGHTBLUE,
	.border_size = 6,
	.border = MVPW_BLACK,
	.margin = 4,
};

static mvpw_menu_item_attr_t tvguide_menu_item_attr = {
	.selectable = true,
	.fg = MVPW_WHITE,
	.bg = MVPW_RGBA(25,112,25,255),
	.checkbox_fg = MVPW_GREEN,
};

void
mvp_tvguide_video_topright(int on)
{

	if (on) {
		av_move(410, 0, 3);
		/*av_move(si.cols/2, 0, 3); */
	} else {
		/*
		PRINTF("av_move(0,0,0): %s [%s,%d]\n", __FUNCTION__, __FILE__, __LINE__);
		*/
		av_move(0, 0, 0);
	}
}

key_set number_keys = {"\0\0\0\0",0};
static void
mvp_tvguide_key_timer(mvp_widget_t * dummy)
{
	int idx = myth_get_chan_index_from_str(tvguide_chanlist, number_keys.keys);

	tvguide_scroll_ofs_y = idx - tvguide_cur_chan_index;
	tvguide_proglist =
		myth_load_guide(mythtv_livetv_program_list, mythtv_database,
										tvguide_chanlist, tvguide_proglist,
										tvguide_cur_chan_index, &tvguide_scroll_ofs_x,
										&tvguide_scroll_ofs_y, tvguide_free_cardids);
	mvp_tvguide_move(MVPW_ARRAY_HOLD, mythtv_livetv_program_list,
									 mythtv_livetv_description);

	PRINTF("**SSDEBUG: key timeout with keys: %s\n", number_keys.keys);
	number_keys.keys[0] = 0;
	number_keys.key_ct = 0;
	mvpw_set_timer(dummy, NULL, 0);
}

int
mvp_tvguide_callback(mvp_widget_t *widget, char key)
{
	int rtrn = 0;
	char * buf;

	switch(key) {
		case MVPW_KEY_GUIDE:
			/* This is where favorites are handled */
			rtrn = 1;
		break;
		case MVPW_KEY_TV:
			PRINTF("In %s hiding guide %d \n", __FUNCTION__, key);
			rtrn = 0;
		break;
		case MVPW_KEY_EXIT:
		case MVPW_KEY_STOP:
			PRINTF("**SSDEBUG: In %s hiding guide %d \n", __FUNCTION__, key);
			showing_guide = 0;
			tvguide_scroll_ofs_x = 0;
			tvguide_scroll_ofs_y = 0;
			mvp_tvguide_video_topright(0);
			mvp_tvguide_hide(mythtv_livetv_program_list,
												mythtv_livetv_description,
												mythtv_livetv_clock);
			/* Update the guide selector to the top left corner */
			myth_set_guide_times(mythtv_livetv_program_list, tvguide_scroll_ofs_x,
													 mythtv_use_12hour_clock);
			tvguide_proglist = 
			myth_load_guide(mythtv_livetv_program_list, mythtv_database,
														tvguide_chanlist, tvguide_proglist,
														tvguide_cur_chan_index, &tvguide_scroll_ofs_x,
														&tvguide_scroll_ofs_y, tvguide_free_cardids);
			rtrn = key == MVPW_KEY_STOP?0:1;
			PRINTF("**SSDEBUG: Guide hidden: %s hiding guide %d \n", __FUNCTION__, key);
		break;
			
		case MVPW_KEY_PAUSE:
			break;
		case MVPW_KEY_PLAY:
			break;
		case MVPW_KEY_REPLAY:
			break;
		case MVPW_KEY_REWIND:
			break;
		case MVPW_KEY_SKIP:
			break;
		case MVPW_KEY_FFWD:
			break;
		case MVPW_KEY_LEFT:
			mvp_tvguide_move(MVPW_ARRAY_LEFT, mythtv_livetv_program_list,
											 mythtv_livetv_description);
			rtrn = 1;
			break;
		case MVPW_KEY_RIGHT:
			mvp_tvguide_move(MVPW_ARRAY_RIGHT, mythtv_livetv_program_list,
											 mythtv_livetv_description);
			rtrn = 1;
			break;
		case MVPW_KEY_ZERO ... MVPW_KEY_NINE:
			/*
			PRINTF("In %s number key %d \n", __FUNCTION__, key);
			*/
			/* Use a timer to get up to 3 keys. */
			if(number_keys.key_ct < 3) {
				sprintf(number_keys.keys, "%s%d", number_keys.keys, key);
				number_keys.key_ct++;
			}
			if(number_keys.key_ct == 3)
				mvpw_set_timer(widget, mvp_tvguide_key_timer, 5);
			else
				mvpw_set_timer(widget, mvp_tvguide_key_timer, 1500);
			rtrn = 1;
			break;
		case MVPW_KEY_MENU:
			break;
		case MVPW_KEY_MUTE:
			break;
		case MVPW_KEY_BLANK:
		case MVPW_KEY_OK:
			if(myth_guide_is_future(mythtv_livetv_program_list, 
															tvguide_scroll_ofs_x) == 0) {
				buf = get_tvguide_selected_channel_str(mythtv_livetv_program_list,
																							 tvguide_chanlist);
				PRINTF("** SSDEBUG: switching to channel: %s\n", buf);
				mythtv_channel_set(buf);
				tvguide_cur_chan_index =
				myth_get_chan_index(tvguide_chanlist, current_prog);
				showing_guide = 0;
				tvguide_scroll_ofs_x = 0;
				tvguide_scroll_ofs_y = 0;
				mvp_tvguide_video_topright(0);
				mvp_tvguide_hide(mythtv_livetv_program_list,
													mythtv_livetv_description,
													mythtv_livetv_clock);
				/* Update the guide to the top left corner */
				myth_set_guide_times(mythtv_livetv_program_list, tvguide_scroll_ofs_x,
														 mythtv_use_12hour_clock);
				tvguide_proglist = 
				myth_load_guide(mythtv_livetv_program_list, mythtv_database,
															tvguide_chanlist, tvguide_proglist,
															tvguide_cur_chan_index, &tvguide_scroll_ofs_x,
															&tvguide_scroll_ofs_y, tvguide_free_cardids);
			}
			else {
				/* Future, item. Schedule it */
				//tvguide_popup_menu = 1;
				//mvpw_show(mythtv_tvguide_menu);
				//mvpw_focus(mythtv_tvguide_menu);
			}
			rtrn = 1;
			break;
		case MVPW_KEY_FULL:
		case MVPW_KEY_PREV_CHAN:
			rtrn = 1;
			break;
		case MVPW_KEY_CHAN_UP:
			mvp_tvguide_move(MVPW_ARRAY_PAGE_UP, mythtv_livetv_program_list,
											 mythtv_livetv_description);
			rtrn = 1;
			break;
		case MVPW_KEY_UP:
			mvp_tvguide_move(MVPW_ARRAY_UP, mythtv_livetv_program_list,
											 mythtv_livetv_description);
			rtrn = 1;
			break;
		case MVPW_KEY_CHAN_DOWN:
			mvp_tvguide_move(MVPW_ARRAY_PAGE_DOWN, mythtv_livetv_program_list,
											 mythtv_livetv_description);
			rtrn = 1;
			break;
		case MVPW_KEY_DOWN:
			mvp_tvguide_move(MVPW_ARRAY_DOWN, mythtv_livetv_program_list,
											 mythtv_livetv_description);
			rtrn = 1;
			break;
		case MVPW_KEY_RECORD:
			rtrn = 1;
			break;
/*
		case MVPW_KEY_RED:
		        PRINTF("Showing 4x3 widget\n");
			mvpw_hide(wss_16_9_image);
			mvpw_show(wss_4_3_image);
		        break;

		case MVPW_KEY_GREEN:
		        PRINTF("Showing 16x9 widget\n");
			mvpw_hide(wss_4_3_image);
			mvpw_show(wss_16_9_image);
		        break;
*/
		case MVPW_KEY_VOL_UP:
		case MVPW_KEY_VOL_DOWN:
			break;
		default:
			PRINTF("button %d\n", key);
			break;
	}

	return rtrn;
}

static
void scroll_callback(mvp_widget_t *widget, int direction)
{
	int changed = 1;

	switch(direction) {
		case MVPW_ARRAY_LEFT:
			PRINTF("** SSDEBUG: scroll to the %s requested\n", "Left");
			tvguide_scroll_ofs_x += 1;
		break;
		case MVPW_ARRAY_RIGHT:
			PRINTF("** SSDEBUG: scroll to the %s requested\n", "Right");
			if(tvguide_scroll_ofs_x > 0)
				tvguide_scroll_ofs_x -= 1;
			else
				changed = 0;
		break;
		case MVPW_ARRAY_UP:
			PRINTF("** SSDEBUG: scroll to the %s requested\n", "Up");
			tvguide_scroll_ofs_y -= 1;
		break;
		case MVPW_ARRAY_DOWN:
			PRINTF("** SSDEBUG: scroll to the %s requested\n", "Down");
			tvguide_scroll_ofs_y += 1;
		break;
		case MVPW_ARRAY_PAGE_UP:
			PRINTF("** SSDEBUG: scroll to the %s requested\n", "Up");
			tvguide_scroll_ofs_y -= 4;
		break;
		case MVPW_ARRAY_PAGE_DOWN:
			PRINTF("** SSDEBUG: scroll to the %s requested\n", "Down");
			tvguide_scroll_ofs_y += 4;
		break;
	}
	if(changed) {
		myth_set_guide_times(mythtv_livetv_program_list, tvguide_scroll_ofs_x,
												 mythtv_use_12hour_clock);
		tvguide_proglist = 
		myth_load_guide(mythtv_livetv_program_list, mythtv_database,
													tvguide_chanlist, tvguide_proglist,
													tvguide_cur_chan_index, &tvguide_scroll_ofs_x,
													&tvguide_scroll_ofs_y, tvguide_free_cardids);
	}
}

static
void tvguide_menu_key_callback(mvp_widget_t *widget, char key)
{
	switch (key) {
	case MVPW_KEY_EXIT:
	case MVPW_KEY_MENU:
		tvguide_popup_menu = 0;
		mvpw_hide(widget);
		mvpw_focus(root);
		break;
	}
}

static void
tvguide_menu_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	//char buf[256];

	//mvpw_hide(widget);

	switch ((int)key) {
	case 1: // Autotune
		//fb_shuffle(1);
		break;
	case 2: // Record
		//snprintf(buf, sizeof(buf), "%d", volume);
		//mvpw_set_dialog_text(volume_dialog, buf);
		//mvpw_show(volume_dialog);
		//mvpw_focus(volume_dialog);
		break;
	case 3: // Cancel
			mvpw_hide(widget);
			mvpw_focus(root);
		break;
	default:
		break;
	}
}

int
mvp_tvguide_init(int edge_left, int edge_top, int edge_right,
								 int edge_bottom)
{
	int x, y, w, h;


	/* The viewport eats up too much real estate for now */
	x = 25; /*edge_left; */
	y = 10; /*edge_top; */
	w = si.cols/2 - 75;
	h = si.rows/2 - 15;

	mythtv_livetv_clock = mvpw_create_text(NULL, x, y, w, 40,
						livetv_header_attr.bg,
						livetv_header_attr.border,
						livetv_header_attr.border_size);
	mvpw_set_text_attr(mythtv_livetv_clock, &livetv_header_attr);

	y += 40;
	h -= 50;

	/* Create the text box that will hold the description text */
	mythtv_livetv_description = mvpw_create_text(NULL, x, y, w, h,
						livetv_description_attr.bg,
						livetv_description_attr.border,
						livetv_description_attr.border_size);
	mvpw_set_text_attr(mythtv_livetv_description, &livetv_description_attr);
	/*
	mvpw_set_text_str(mythtv_livetv_description,
		"This is a test of the description widget which needs to be modified to have the time and some mvpmc marketing above it and then the description below. And jus to see what happens when we exceed the available space since our other widgets are misbehaving we're going to keep adding stuff till it over flows.");
	*/

	h += 50;

	mythtv_livetv_program_list = mvpw_create_array(NULL,
				25, h, si.cols-50, si.rows/2-10, 0,
				livetv_program_list_attr.array_border,
				livetv_program_list_attr.border_size);
	mvpw_set_array_attr(mythtv_livetv_program_list, &livetv_program_list_attr);
	/* Temporaray settings to test the functionality
	for(i=0;i<4;i++)
		mvpw_set_array_row(mythtv_livetv_program_list, i, row_strings[i], NULL);
	for(i=0;i<4;i++)
		mvpw_set_array_col(mythtv_livetv_program_list, i, col_strings[i], NULL);
	for(i=0;i<4;i++) {
		for(j=0;j<3;j++)
			mvpw_set_array_cell(mythtv_livetv_program_list, j, i,
				cell_strings[i*3+j], NULL);
	}

	*/
	mvpw_hilite_array_cell(mythtv_livetv_program_list,0,0,1);

	mvpw_set_array_scroll(mythtv_livetv_program_list, scroll_callback);

	/* Create the menu for future programs (schedule or autotune) */

	h = 4 * FONT_HEIGHT(tvguide_popup_attr);
	w = 200;
	x = (si.cols - w) / 2;
	y = (si.rows - h) / 2;
	//x = (edge_right - edge_left - w) / 2;
	//y = (edge_bottom - edge_top - h) / 2;

	mythtv_tvguide_menu =
						mvpw_create_menu(NULL, x, y, w, h,
				  	tvguide_popup_attr.bg, tvguide_popup_attr.border,
				  	tvguide_popup_attr.border_size);

	tvguide_popup_attr.checkboxes = 0;
	mvpw_set_menu_attr(mythtv_tvguide_menu, &tvguide_popup_attr);
	mvpw_set_menu_title(mythtv_tvguide_menu, "Program Menu");

	mvpw_set_key(mythtv_tvguide_menu, tvguide_menu_key_callback);

	tvguide_menu_item_attr.select = tvguide_menu_select_callback;
	tvguide_menu_item_attr.fg = tvguide_popup_attr.fg;
	tvguide_menu_item_attr.bg = tvguide_popup_attr.bg;
	mvpw_add_menu_item(mythtv_tvguide_menu, "Auto Tune",
			   (void*)1, &tvguide_menu_item_attr);
	mvpw_add_menu_item(mythtv_tvguide_menu, "Record",
			   (void*)2, &tvguide_menu_item_attr);
	mvpw_add_menu_item(mythtv_tvguide_menu, "Cancel",
			   (void*)3, &tvguide_menu_item_attr);


	return 1;
}

/*
 *This function is called to update the tvguide clock which is displayed
 * at the top left hand corner of the guide.
 */
static void
mvp_tvguide_clock_timer(mvp_widget_t * widget)
{
	int next = 60000;
	time_t curtime;
	struct tm * now;
	char tm_buf[16];

	curtime = time(NULL);
	now = localtime(&curtime);
	if(now->tm_sec < 60)
		next = (60 - (now->tm_sec))*1000;
	mvpw_set_timer(mythtv_livetv_clock, mvp_tvguide_clock_timer, next);
	if (mythtv_use_12hour_clock)
		strftime(tm_buf, 16, "%I:%M %P", now);
	else
		sprintf(tm_buf, "%02d:%02d", now->tm_hour, now->tm_min);

	mvpw_set_text_str(widget, tm_buf);
}

/* This function is called periodically to synchronise the guide */
/* updating recorder statuses and re-loading data as the time */
/* shifts outside of the currently stored view. It might be cool */
/* to make this its own thread but for now, it will be a callback */
/* that re-schedules itself to be called in one minute each time */
/* it gets called. */
static void
mvp_tvguide_timer(mvp_widget_t * widget)
{
	static int tvguide_cur_time = -1;
	int next = 60000;
	struct tm * now;
	time_t curtime;

	/*
	PRINTF("** SSDEBUG: %s called in %s, on line %d\n", __FUNCTION__, __FILE__,
				 __LINE__);
	*/

	/* Ideally, this should get adjusted to fall on an exact minute */
	/* second boudary. For now, this is good enough */

	pthread_mutex_lock(&myth_mutex);

	/* If we haven't done so yet, get the index in our local cache of
	 * the channel that's currently playing on live tv
	 */
	if(tvguide_cur_time == -1) {
		tvguide_cur_chan_index =
						myth_get_chan_index(tvguide_chanlist, current_prog);

		PRINTF("** SSDEBUG: current channel index is: %d\n",
						tvguide_cur_chan_index);

 		tvguide_cur_time = 1;
	}

	/* Reset the timer to synch with the minute mark */
	curtime = time(NULL);
	now = localtime(&curtime);
	if(now->tm_sec < 60) /* In case of leap seconds just use 60 */
		next = (60 - (now->tm_sec))*1000;

	/* Reset the timer for a minute from now in most cases except the first */
	mvpw_set_timer(mythtv_livetv_program_list, mvp_tvguide_timer, next);

	/* Determine which recorders are free and save the bit array of
	 * free recorders for future reference in displaying the guide.
	 * approach to find free recorders. Possibly a database query.
	 */

	tvguide_free_cardids = myth_tvguide_get_free_cardids(control);
	tvguide_free_cardids |= myth_tvguide_get_active_card(mythtv_recorder);

	/*
	PRINTF("** SSDEBUG: bitmap of usable recorders %ld\n",tvguide_free_cardids);
	*/

	/* Set the times in the guide and possibly the entire body if the
	 * the next 30 minute interval has been hit.
	 */

	if(myth_set_guide_times(mythtv_livetv_program_list, tvguide_scroll_ofs_x,
												  mythtv_use_12hour_clock)) {
		tvguide_proglist = 
		myth_load_guide(mythtv_livetv_program_list, mythtv_database,
													tvguide_chanlist, tvguide_proglist,
													tvguide_cur_chan_index, &tvguide_scroll_ofs_x,
													&tvguide_scroll_ofs_y, tvguide_free_cardids);
	}
	else {
		myth_guide_set_channels(mythtv_livetv_program_list, tvguide_chanlist,
														tvguide_cur_chan_index, tvguide_scroll_ofs_y,
														tvguide_free_cardids);
	}

	pthread_mutex_unlock(&myth_mutex);
}

int
mvp_tvguide_start(void)
{
	pthread_mutex_lock(&myth_mutex);
	int rtrn = 0;

	PRINTF("** SSDEBUG: loading channels\n");
	/* Load the entire list of channels available on all recorders 		*/
	/* combine all identical entries with a list of recorders for each */
	/* and create from scratch so free if it exists */

	//tvguide_chanlist = myth_release_chanlist(tvguide_chanlist);
	tvguide_chanlist = myth_tvguide_load_channels(mythtv_database,
																								mythtv_tvguide_sort_desc);
	if(tvguide_chanlist == NULL) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s loading channels failed\n", __FUNCTION__);
		rtrn = -1;
	}
	PRINTF("** SSDEBUG: channels loaded\n");
	pthread_mutex_unlock(&myth_mutex);

	/* Schedule periodic updates of the tv guide the current simple
	 * implementation just checks every minute. In the future it
	 * might be easier to just schedule the timer at the next half
	 * hour interval.
		mvp_tvguide_timer(mythtv_livetv_program_list);
	 */
	mvpw_set_timer(mythtv_livetv_program_list, mvp_tvguide_timer, 100);
	PRINTF("** SSDEBUG: timer started\n");

	mvpw_set_timer(mythtv_livetv_clock, mvp_tvguide_clock_timer, 2000);

	return 0;
}

int
mvp_tvguide_stop(void)
{
	mvpw_set_timer(mythtv_livetv_program_list, NULL, 0);
	mvpw_set_timer(mythtv_livetv_clock, NULL, 0);
	tvguide_chanlist = myth_release_chanlist(tvguide_chanlist);
	tvguide_proglist = myth_release_proglist(tvguide_proglist);

	return 0;
}

