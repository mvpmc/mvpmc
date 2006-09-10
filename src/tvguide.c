/*
 *  Copyright (C) 2006, Sergio Slobodrian
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

#ident "$Id: video.c,v 1.76 2006/02/16 01:11:40 gettler Exp $"

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
cmyth_tvguide_progs_t  tvguide_proglist = NULL;
int tvguide_cur_chan_index;
int tvguide_scroll_ofs_x = 0;
int tvguide_scroll_ofs_y = 0;
long tvguide_free_cardids = 0;

typedef struct {
	char keys[4];
	int key_ct;
} key_set;


/*
 * livetv guide attributes
 */

/* Clock window */
static mvpw_text_attr_t livetv_clock_attr = {
	.wrap = 1,
	.pack = 1,
	.justify = MVPW_TEXT_LEFT,
	.margin = 9,
	.font = FONT_LARGE,
	.fg = MVPW_WHITE,
	.bg = MVPW_MIDNIGHTBLUE,
	.border = MVPW_BLACK,
	.border_size = 0,
};

/* Description window */
static mvpw_text_attr_t livetv_description_attr = {
	.wrap = 1,
	.pack = 1,
	.justify = MVPW_TEXT_LEFT,
	.margin = 9,
	.font = FONT_LARGE,
	.fg = MVPW_WHITE,
	.bg = MVPW_DARKGREY,
	.border = MVPW_BLACK,
	.border_size = 0,
};

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
	.col_label_bg = MVPW_RGBA(25,112,25,255),
	.cell_fg = MVPW_WHITE,
	.cell_bg = MVPW_MIDNIGHTBLUE,
	.hilite_fg = MVPW_BLACK,
	.hilite_bg = MVPW_ALMOSTWHITEGREY,
	.cell_rounded = 0,
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
	char buf[20];

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
			myth_set_guide_times(mythtv_livetv_program_list, tvguide_scroll_ofs_x);
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
			if(tvguide_scroll_ofs_x == 0) {
				sprintf(buf, "%d",
								get_tvguide_selected_channel(mythtv_livetv_program_list));
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
				myth_set_guide_times(mythtv_livetv_program_list, tvguide_scroll_ofs_x);
				tvguide_proglist = 
				myth_load_guide(mythtv_livetv_program_list, mythtv_database,
															tvguide_chanlist, tvguide_proglist,
															tvguide_cur_chan_index, &tvguide_scroll_ofs_x,
															&tvguide_scroll_ofs_y, tvguide_free_cardids);
			}
			else {
				/* Future, item. Schedule it */
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
		myth_set_guide_times(mythtv_livetv_program_list, tvguide_scroll_ofs_x);
		tvguide_proglist = 
		myth_load_guide(mythtv_livetv_program_list, mythtv_database,
													tvguide_chanlist, tvguide_proglist,
													tvguide_cur_chan_index, &tvguide_scroll_ofs_x,
													&tvguide_scroll_ofs_y, tvguide_free_cardids);
	}
}

int
mvp_tvguide_init(int edge_left, int edge_top, int edge_right,
								 int edge_bottom)
{
	int x, y, w, h;
	/*
	char * col_strings[] = {"Fri 4/8", "9:00", "9:30", "10:00"};
	char * row_strings[] = {"627\nSPACE", "615\nA&E", "525\nANIML", "524\nGEO"};
	char * cell_strings[] = {"Enterprise", "Supernatural", "Program",
			"Windtalkers", "Herbie", "Cheetahs", "Mad Mike", "Untamed NA",
			"Bevis & Butthead", "Hitchhiker's Guide",
			"So long and thanks for all the fish",
			"Fourty Two"};
	*/


	/* The viewport eats up too much real estate for now */
	x = 25; /*edge_left; */
	y = 10; /*edge_top; */
	w = si.cols/2 - 75;
	h = si.rows/2 - 15;

	mythtv_livetv_clock = mvpw_create_text(NULL, x, y, w, 20,
						livetv_description_attr.bg,
						livetv_description_attr.border,
						livetv_description_attr.border_size);
	mvpw_set_text_attr(mythtv_livetv_clock, &livetv_clock_attr);

	y += 20;
	h -= 20;

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

	return 1;
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

	/*
	PRINTF("** SSDEBUG: %s called in %s, on line %d\n", __FUNCTION__, __FILE__,
				 __LINE__);
	*/

	/* Reset the timer for a minute from now */
	/* Ideally, this should get adjusted to fall on an exact 30 */
	/* second boudary. For now, this is good enough */
	mvpw_set_timer(mythtv_livetv_program_list, mvp_tvguide_timer, 60000);

	pthread_mutex_lock(&myth_mutex);

	/* If we haven't done so yet, get the index in our local cache of
	 * the channel that's currently playing on live tv
	 */
	if(tvguide_cur_time == -1) {
		tvguide_cur_chan_index =
						myth_get_chan_index(tvguide_chanlist, current_prog);
		/*
		PRINTF("** SSDEBUG: current channel index is: %d\n",
						tvguide_cur_chan_index);
		*/
 		tvguide_cur_time = 1;
	}

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

	if(myth_set_guide_times(mythtv_livetv_program_list, tvguide_scroll_ofs_x)) {
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

	tvguide_chanlist = myth_release_chanlist(tvguide_chanlist);
	tvguide_chanlist = myth_load_channels2(mythtv_database);
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


	return 0;
}

int
mvp_tvguide_stop(void)
{
	mvpw_set_timer(mythtv_livetv_program_list, NULL, 0);

	return 0;
}

