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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <mvp_refmem.h>
#include <cmyth.h>

#include "mvpmc.h"
#include "mythtv.h"
#include "config.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

int new_live_tv;
extern cmyth_chanlist_t tvguide_chanlist;
extern cmyth_tvguide_progs_t tvguide_proglist;
extern mvp_widget_t *mythtv_slow_connect;
extern int tvguide_cur_chan_index;
extern int tvguide_scroll_ofs_x;
extern int tvguide_scroll_ofs_y;
extern long tvguide_free_cardids;
extern mvp_atomic_t mythtv_prevent_request_block;
extern pthread_mutex_t request_block_mutex;

static mvpw_menu_item_attr_t item_attr = {
	.selectable = 1,
	.fg = MVPW_WHITE,
	.bg = MVPW_BLACK,
	.checkbox_fg = MVPW_GREEN,
};

volatile int current_livetv;

#define MAX_TUNER	16
struct livetv_proginfo {
	int rec_id;
	int busy;
	char *chan;
	char *channame;
};

struct livetv_prog {
	char *title;
	char *subtitle;
	char *description;
	char *start;
	char *end;
	int count;
	struct livetv_proginfo pi[MAX_TUNER];
};

static struct livetv_prog *livetv_list = NULL;
static int livetv_count = 0;

static int get_livetv_programs(void);

static int livetv_open(void);
static long long livetv_size(void);
void livetv_select_callback(mvp_widget_t*, char*, void*);

static video_callback_t livetv_functions = {
	.open      = livetv_open,
	.read      = mythtv_read,
	.read_dynb = NULL,
	.seek      = mythtv_seek,
	.size      = livetv_size,
	.notify    = NULL,
	.key       = NULL,
	.halt_stream = NULL,
};

static int
livetv_compare(const void *a, const void *b)
{
	const struct livetv_prog *x, *y;
	int X, Y;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	x = ((const struct livetv_prog*)a);
	y = ((const struct livetv_prog*)b);
	X = atoi(x->pi[0].chan);
	Y = atoi(y->pi[0].chan);

	if (X < Y) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1 }\n",
			    __FUNCTION__, __FILE__, __LINE__);

		return -1;
	} else if (X > Y) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 1}\n",
			    __FUNCTION__, __FILE__, __LINE__);

		return 1;
	} else {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return 0;
	}
}

int
mythtv_livetv_chain_update(char * buf)
{
	char * pfx = "LIVETV_CHAIN UPDATE ";
	char * chainid = buf + strlen(pfx);

	/* Exclusive access required */
	pthread_mutex_lock(&myth_mutex);

	/* Check if we've got an active recorder */
	if(mythtv_recorder) {
		cmyth_livetv_chain_update(mythtv_recorder, chainid, mythtv_tcp_program);
	}

	pthread_mutex_unlock(&myth_mutex);

	return 0;
}

static void
prog_update_callback(cmyth_proginfo_t prog)
{
	cmyth_proginfo_t loc_prog = ref_hold(prog);
	CHANGE_GLOBAL_REF(current_prog, loc_prog);
	ref_release(loc_prog);
}

static void
slow_to_connect_callback(mvp_widget_t * widget)
{
	mvpw_show(widget);
	mvpw_set_timer(widget, NULL, 0);
}

static int
mythtv_new_livetv_start(cmyth_recorder_t rec)
{
	double rate;
	char *rb_file;
	char *msg = NULL;
	cmyth_proginfo_t loc_prog = NULL;
	cmyth_conn_t ctrl = ref_hold(control);
	char *path;


	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);

	new_live_tv = 1;


	if (playing_via_mythtv && (mythtv_file || mythtv_recorder))
		mythtv_stop();

	if (mythtv_livetv) {
		fprintf(stderr, "Live TV already active\n");
		mvpw_show(mythtv_logo);
		mvpw_show(mythtv_browser);
		mvpw_focus(mythtv_browser);
		pthread_mutex_lock(&myth_mutex);
		get_livetv_programs();
		pthread_mutex_unlock(&myth_mutex);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return 0;
	}

	fprintf(stderr, "Starting New Live TV!\n");

	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	pthread_mutex_lock(&myth_mutex);

	mythtv_livetv = 1;
	playing_via_mythtv = 1;
	if (mythtv_ringbuf) {
		video_functions = &file_functions;
	} else {
		video_functions = &livetv_functions;
	}

	mvpw_set_timer(mythtv_slow_connect, slow_to_connect_callback, 10000);
	if((rec = cmyth_spawn_live_tv(rec, 16*1024,mythtv_tcp_program,
																prog_update_callback, &msg)) == NULL)
		goto err;
	mvpw_set_timer(mythtv_slow_connect, NULL, 0);
	mvpw_hide(mythtv_slow_connect);

	//printf("** SSDEBUG: Spawned live tv\n");
	if (cmyth_recorder_is_recording(rec) != 1) {
		msg = "LiveTV not recording.";
		goto err;
	}

	if (cmyth_recorder_get_framerate(rec, &rate) != 0) {
		msg = "Get framerate failed.";
		goto err;
	}

	fprintf(stderr, "recorder framerate is %5.2f\n", rate);


	rb_file = (char *) cmyth_recorder_get_filename(rec);
	/*
	 * Change current.  The thing about 'current' is that it is a
	 * global that is not allocated as reference counted and it is
	 * allocated and freed outside this file.  If / when it turns
	 * into reference counted space, it will be much cleaner and
	 * safer to hold it in a local, release and change it and then
	 * release the local.  For now we take a chance on a non-held
	 * reference being destroyed.
	 */
	if (mythtv_ringbuf) {
		char *tmp;
		path = current;
		tmp = malloc(strlen(mythtv_ringbuf) + strlen(rb_file) + 2);
		sprintf(tmp, "%s/%s", mythtv_ringbuf, rb_file);
		current = tmp;
		free(path);
	} else {
		path = current;
		current = strdup(rb_file);
		free(path);
	}
	ref_release(rb_file);

	// get the information about the current programme
	// we assume last used structure is cleared already...
	//
	loc_prog = cmyth_recorder_get_cur_proginfo(rec);
	PRINTF("** SSDEBUG: the loc_prog is: %p\n", loc_prog);
	CHANGE_GLOBAL_REF(current_prog, loc_prog);
	ref_release(loc_prog);

	CHANGE_GLOBAL_REF(mythtv_recorder, rec);
	/*
	mvpw_show(mythtv_browser);
	mvpw_focus(mythtv_browser);
	*/

	demux_reset(handle);
	demux_attr_reset(handle);
	av_play();
	video_play(root);

	video_thumbnail(AV_THUMBNAIL_EIGTH,VID_THUMB_BOTTOM_RIGHT);

	mythtv_fullscreen();
	video_thumbnail(AV_THUMBNAIL_EIGTH,VID_THUMB_BOTTOM_RIGHT);
	mythtv_fullscreen();

	// enable program info widget
	//
	add_osd_widget(mythtv_program_widget, OSD_PROGRAM,
		       osd_settings.program, NULL);
	mvpw_hide(mythtv_description);
	/*
	mvpw_hide(mythtv_browser);
	*/

#if 0
	ref_release(ctrl);
	ref_release(rec);
#endif
	ref_release(rec);
	running_mythtv = 1;
	pthread_mutex_unlock(&myth_mutex);

	/* Fire up the tv_guide updater/synchroniser */
	PRINTF("** SSDEBUG: Firing up the TV guide functionality\n");
	mvp_tvguide_start();
	
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return 0;

 err:
	pthread_mutex_unlock(&myth_mutex);

	mythtv_livetv = 0;
	if (msg)
		gui_error(msg);

	ref_release(ctrl);
	ref_release(rec);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return -1;
}

static int
mythtv_livetv_start(int *tuner)
{
	double rate;
	char *rb_file;
	char *msg = NULL, buf[128], t[16];
	int c, i, id = 0;
	cmyth_proginfo_t loc_prog = NULL;
	cmyth_conn_t ctrl = ref_hold(control);
	cmyth_recorder_t rec = NULL;
	char *path;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);

	new_live_tv = 0;

	if (playing_via_mythtv && (mythtv_file || mythtv_recorder))
		mythtv_stop();

	if (mythtv_livetv) {
		fprintf(stderr, "Live TV already active\n");
		mvpw_show(mythtv_logo);
		mvpw_show(mythtv_browser);
		mvpw_focus(mythtv_browser);
		pthread_mutex_lock(&myth_mutex);
		get_livetv_programs();
		pthread_mutex_unlock(&myth_mutex);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return 0;
	}

	fprintf(stderr, "Starting Live TV...\n");

	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	pthread_mutex_lock(&myth_mutex);

	mythtv_livetv = 1;
	playing_via_mythtv = 1;
	if (mythtv_ringbuf) {
		video_functions = &file_functions;
	} else {
		video_functions = &livetv_functions;
	}

	if ((c = cmyth_conn_get_free_recorder_count(ctrl)) < 0) {
		mythtv_shutdown(1);
		goto err;
	}
	printf("Found %d free recorders\n", c);

	if (tuner[0]) {
		int count = 0;
		for (i=0; i<MAX_TUNER && tuner[i]; i++) {
			fprintf(stderr, "Looking for tuner %d\n", tuner[i]);
                        count++; 
			if ((rec = cmyth_conn_get_recorder_from_num(ctrl,
								    tuner[i]))
			    == NULL) {
				continue;
			}
			if(cmyth_recorder_is_recording(rec) == 1) {
				ref_release(rec);
				rec = NULL;
				continue;
			}
			id = tuner[i];
			break;
		}
		if (id == 0) {
			/*
			 * None of the tuners are free, so display a good error
			 * message for the user.
			 */
			if (count == 1) {
				snprintf(buf, sizeof(buf),
					 "Tuner %d is currently unavailable.",
					 tuner[0]);
			} else {
				snprintf(buf, sizeof(buf), "Tuners ");
				for (i=0; i<count; i++) {
					if ((i != 0) && (count > 2))
						strcat(buf, ",");
					if (i == (count-1))
						strcat(buf, " and");
					snprintf(t, sizeof(t),
						 " %d", tuner[i]);
					strcat(buf, t);
				}
				strcat(buf, " are currently unavailable.");
			}
			msg = buf;
			goto err;
		}
	} else {
		fprintf(stderr, "Looking for any free recorder\n");
		if ((rec = cmyth_conn_get_free_recorder(ctrl)) == NULL) {
			msg = "Failed to get free recorder.";
			goto err;
		}
	}

	mvpw_set_timer(mythtv_slow_connect, slow_to_connect_callback, 10000);
	if((rec = cmyth_spawn_live_tv(rec, 16*1024,mythtv_tcp_program,
																prog_update_callback, &msg)) == NULL)
		goto err;
	mvpw_set_timer(mythtv_slow_connect, NULL, 0);
	mvpw_hide(mythtv_slow_connect);

	if (cmyth_recorder_is_recording(rec) != 1) {
		msg = "LiveTV not recording.";
		goto err;
	}

	if (cmyth_recorder_get_framerate(rec, &rate) != 0) {
		msg = "Get framerate failed.";
		goto err;
	}

	fprintf(stderr, "recorder framerate is %5.2f\n", rate);

	rb_file = (char *) cmyth_recorder_get_filename(rec);
	/*
	 * Change current.  The thing about 'current' is that it is a
	 * global that is not allocated as reference counted and it is
	 * allocated and freed outside this file.  If / when it turns
	 * into reference counted space, it will be much cleaner and
	 * safer to hold it in a local, release and change it and then
	 * release the local.  For now we take a chance on a non-held
	 * reference being destroyed.
	 */
	if (mythtv_ringbuf) {
		char *tmp;
		path = current;
		tmp = malloc(strlen(mythtv_ringbuf) + strlen(rb_file) + 2);
		sprintf(tmp, "%s/%s", mythtv_ringbuf, rb_file);
		current = tmp;
		free(path);
	} else {
		path = current;
		current = strdup(rb_file);
		free(path);
	}
	ref_release(rb_file);

	// get the information about the current programme
	// we assume last used structure is cleared already...
	//
	loc_prog = cmyth_recorder_get_cur_proginfo(rec);
	CHANGE_GLOBAL_REF(current_prog, loc_prog);
	ref_release(loc_prog);

	CHANGE_GLOBAL_REF(mythtv_recorder, rec);
	// This appears to be redundant and takes a bunch of time slowing down
	// the launch of live TV.
	get_livetv_programs();

	mvpw_show(mythtv_browser);
	mvpw_focus(mythtv_browser);

	demux_reset(handle);
	demux_attr_reset(handle);
	video_thumbnail(AV_THUMBNAIL_EIGTH,VID_THUMB_BOTTOM_RIGHT);
	av_play();
	video_play(root);

	// enable program info widget
	//
	add_osd_widget(mythtv_program_widget, OSD_PROGRAM,
		       osd_settings.program, NULL);
	mvpw_hide(mythtv_description);

#if 0
	ref_release(ctrl);
	ref_release(rec);
#endif
	ref_release(rec);
	running_mythtv = 1;
	pthread_mutex_unlock(&myth_mutex);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return 0;

 err:
	pthread_mutex_unlock(&myth_mutex);

	mythtv_livetv = 0;
	if (msg)
		gui_error(msg);

	ref_release(ctrl);
	ref_release(rec);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return -1;
}

int
mythtv_livetv_stop(void)
{
	int ret = -1;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (!mythtv_livetv) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			  __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	fprintf(stderr, "Stopping Live TV\n");

	mvp_atomic_inc(&mythtv_prevent_request_block);
	busy_start();
	pthread_mutex_lock(&request_block_mutex);

	pthread_mutex_lock(&myth_mutex);

	if (!mythtv_ringbuf) {
		playing_via_mythtv = 0;
		running_mythtv = 0;
		close_mythtv = 0;
	}

	mythtv_livetv = 0;

	video_clear();
	mvpw_set_idle(NULL);
	mvpw_set_timer(root, NULL, 0);

	if (cmyth_recorder_stop_livetv(mythtv_recorder) != 0) {
		fprintf(stderr, "stop livetv failed\n");
		goto fail;
	}

	if (cmyth_recorder_done_ringbuf(mythtv_recorder) != 0) {
		fprintf(stderr, "done ringbuf failed\n");
		goto fail;
	}

	CHANGE_GLOBAL_REF(mythtv_recorder, NULL);
	CHANGE_GLOBAL_REF(current_prog, NULL);

	if(new_live_tv) {
		mvp_tvguide_stop();
		PRINTF("** SSDEBUG: Tvguide stopped: %s  %d \n", __FUNCTION__, __LINE__);
	}

	ret = 0;

 fail:
	mythtv_livetv = 0;
	pthread_mutex_unlock(&myth_mutex);

	mvp_atomic_dec(&mythtv_prevent_request_block);
	pthread_mutex_unlock(&request_block_mutex);


	busy_end();
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) %d}\n",
		    __FUNCTION__, __FILE__, __LINE__, ret);
	return ret;
}

int __change_channel(direction)
{
	int ret = 0;
	cmyth_proginfo_t loc_prog = NULL;
	cmyth_conn_t ctrl = ref_hold(control);
	cmyth_recorder_t rec = ref_hold(mythtv_recorder);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	changing_channel = 1;

	mvp_atomic_inc(&mythtv_prevent_request_block);
	busy_start();
	pthread_mutex_lock(&request_block_mutex);
	video_clear();
	pthread_mutex_lock(&myth_mutex);

	if (cmyth_recorder_pause(rec) < 0) {
		fprintf(stderr, "channel change (pause) failed\n");
		ret = -1;
		goto out;
	}

	if (cmyth_recorder_change_channel(rec, direction) < 0) {
		fprintf(stderr, "channel change failed\n");
		ret = -1;
		goto out;
	}

	/*
	 * Force myth to delete the ringbuffer if we are playing via NFS,
	 * so that we don't risk getting old file data.
	 */
	if (mythtv_ringbuf) {
#if 0
		if (cmyth_recorder_stop_livetv(rec) != 0) {
			fprintf(stderr, "stop livetv failed\n");
			ret = -1;
			goto out;
		}

		/*
		 * XXX: How do we restart live tv?  Tearing down the connection
		 *      and recreating it seems like overkill, and for all I
		 *      know you might end up on a different tuner.
		 */
#endif
		sleep(6);
	}

	loc_prog = cmyth_recorder_get_cur_proginfo(rec);
	CHANGE_GLOBAL_REF(current_prog, loc_prog);
	ref_release(loc_prog);

	// we need to reset the ringbuffer reader to the start of the file
	// since the backend always resets the pointer.
	// but we must be sure there is correct data on the buffer.
	//
	demux_reset(handle);
	demux_attr_reset(handle);
	av_move(0, 0, 0);
	av_play();
	video_play(root);

	cmyth_livetv_chain_switch_last(rec);
	/* This should probably be in a library or in the tvguide
	 * source file. This is way too much code for this location.
	 * TODO.
	 */
	if(new_live_tv) {
		int idx = myth_get_chan_index(tvguide_chanlist, current_prog);

		if (idx == -1) {
			idx = tvguide_cur_chan_index = direction == CHANNEL_DIRECTION_UP?
						tvguide_cur_chan_index+1:tvguide_cur_chan_index-1;
		}
		else {
			tvguide_cur_chan_index = idx;
		}

		tvguide_scroll_ofs_y = idx - tvguide_cur_chan_index;
		tvguide_proglist =
			myth_load_guide(mythtv_livetv_program_list, mythtv_database,
											tvguide_chanlist, tvguide_proglist,
											tvguide_cur_chan_index, &tvguide_scroll_ofs_x,
											&tvguide_scroll_ofs_y, tvguide_free_cardids);
		mvp_tvguide_move(MVPW_ARRAY_HOLD, mythtv_livetv_program_list,
									 	mythtv_livetv_description);
	}

 out:
	mvp_atomic_dec(&mythtv_prevent_request_block);
	pthread_mutex_unlock(&request_block_mutex);
	ref_release(ctrl);
	ref_release(rec);
	changing_channel = 0;
	busy_end();
	pthread_mutex_unlock(&myth_mutex);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) %d}\n",
		    __FUNCTION__, __FILE__, __LINE__, ret);
        return ret;
}

int
mythtv_channel_up(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return __change_channel(CHANNEL_DIRECTION_UP);
}

int
mythtv_channel_down(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return __change_channel(CHANNEL_DIRECTION_DOWN);
}

int
mythtv_channel_set(char * channame)
{
	int ret = 0;
	cmyth_proginfo_t loc_prog = NULL;
	cmyth_conn_t ctrl = ref_hold(control);
	cmyth_recorder_t rec = ref_hold(mythtv_recorder);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	changing_channel = 1;
        
	mvp_atomic_inc(&mythtv_prevent_request_block);
	busy_start();
	pthread_mutex_lock(&request_block_mutex);
	video_clear();
	pthread_mutex_lock(&myth_mutex);

	if (cmyth_recorder_pause(rec) < 0) {
		fprintf(stderr, "channel change (pause) failed\n");
		ret = -1;
		goto out;
	}

	if (cmyth_recorder_set_channel(rec, channame) < 0) {
		fprintf(stderr, "channel change failed\n");
		ret = -1;
		goto out;
	}

	/*
	 * Force myth to delete the ringbuffer if we are playing via NFS,
	 * so that we don't risk getting old file data.
	 */
	if (mythtv_ringbuf) {
#if 0
		if (cmyth_recorder_stop_livetv(rec) != 0) {
			fprintf(stderr, "stop livetv failed\n");
			ret = -1;
			goto out;
		}

		/*
		 * XXX: How do we restart live tv?  Tearing down the connection
		 *      and recreating it seems like overkill, and for all I
		 *      know you might end up on a different tuner.
		 */
#endif
		sleep(6);
	}

	loc_prog = cmyth_recorder_get_cur_proginfo(rec);
	CHANGE_GLOBAL_REF(current_prog, loc_prog);
	ref_release(loc_prog);

	// we need to reset the ringbuffer reader to the start of the file
	// since the backend always resets the pointer.
	// but we must be sure there is correct data on the buffer.
	//
	demux_reset(handle);
	demux_attr_reset(handle);
	printf("av_move(0,0,0): %s [%s,%d]\n", __FUNCTION__, __FILE__, __LINE__);
	av_move(0, 0, 0);
	av_play();
	video_play(root);

	cmyth_livetv_chain_switch_last(rec);

 out:
	mvp_atomic_dec(&mythtv_prevent_request_block);
	pthread_mutex_unlock(&request_block_mutex);
	ref_release(ctrl);
	ref_release(rec);
	changing_channel = 0;
	busy_end();
	pthread_mutex_unlock(&myth_mutex);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) %d}\n",
		    __FUNCTION__, __FILE__, __LINE__, ret);
        return ret;
}

static long long
livetv_size(void)
{
	long long seek_pos;
	cmyth_recorder_t rec = ref_hold(mythtv_recorder);

	/*
	 * XXX: How do we get the program size for live tv?
	 */

	pthread_mutex_lock(&myth_mutex);
	seek_pos = cmyth_livetv_seek(rec, 0, SEEK_CUR);
	PRINTF("%s(): pos %lld\n", __FUNCTION__, seek_pos);
	pthread_mutex_unlock(&myth_mutex);

	ref_release(rec);
	return (seek_pos+(1024*1024*500));
}

static int
livetv_open(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (playing_via_mythtv == 0) {
		playing_via_mythtv = 1;
		fprintf(stderr, "starting mythtv live tv transfer\n");
	}

	playing_file = 1;

	pthread_cond_signal(&myth_cond);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return 0;
}

void
livetv_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	char *channame = NULL;
	int i, prog = (int)key;
	int id = -1;
	int tuner_change = 1, tuner[MAX_TUNER];
	struct livetv_proginfo *pi;
	cmyth_proginfo_t loc_prog = NULL;
	cmyth_conn_t ctrl = ref_hold(control);
	cmyth_recorder_t rec = ref_hold(mythtv_recorder);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	memset(tuner, 0, sizeof(tuner));

	switch_hw_state(MVPMC_STATE_MYTHTV);

	busy_start();
	mythtv_clear_channel();

	if (mythtv_ringbuf)
		video_functions = &file_functions;
	else
		video_functions = &livetv_functions;


	if (mythtv_livetv) {
		id = cmyth_recorder_get_recorder_id(rec);
		for (i=0; i<livetv_list[prog].count; i++) {
			pi = &livetv_list[prog].pi[i];
			if (id == pi->rec_id) {
				tuner_change = 0;
				channame = ref_hold(pi->chan);
				break;
			}
		}
	} else {
		channame = ref_hold(livetv_list[prog].pi[0].chan);
		for (i=0; i<livetv_list[prog].count; i++) {
			tuner[i] = livetv_list[prog].pi[i].rec_id;
			printf("enable livetv tuner %d chan '%s'\n",
			       tuner[i], channame);
		}
		tuner_change = 0;
	}

	if (tuner_change && (id != -1)) {
		for (i=0; i<livetv_list[prog].count; i++)
			tuner[i] = livetv_list[prog].pi[i].rec_id;
		ref_release(channame);
		channame = ref_hold(livetv_list[prog].pi[0].chan);
		fprintf(stderr, "switch from tuner %d to %d\n", id, tuner[0]);
		mythtv_livetv_stop();
	}

	if (mythtv_ringbuf)
		mythtv_livetv_stop();

	if (mythtv_livetv == 0) {
		fprintf(stderr, "Live TV not active!\n");
		if (mythtv_livetv_start(tuner) != 0) {
			goto out;
		}
		ref_release(rec);
		ref_release(ctrl);
		rec = ref_hold(mythtv_recorder);
	}

	if (item)
		fprintf(stderr, "%s(): change channel '%s' '%s'\n",
		       __FUNCTION__, channame, item);

	changing_channel = 1;

	pthread_mutex_lock(&myth_mutex);

	if (cmyth_recorder_pause(rec) < 0) {
		fprintf(stderr, "channel change (pause) failed\n");
		goto unlock;
	}

	if (cmyth_recorder_set_channel(rec, channame) < 0) {
		fprintf(stderr, "channel change failed!\n");
		goto unlock;
	}

	loc_prog = cmyth_recorder_get_cur_proginfo(rec);
	CHANGE_GLOBAL_REF(current_prog, loc_prog);
	ref_release(loc_prog);

	demux_reset(handle);
	demux_attr_reset(handle);
	av_play();
	video_play(root);

	if (widget)
		mythtv_fullscreen();

	i = 0;
	while (mvpw_menu_set_item_attr(mythtv_browser,
				       (void*)i, &item_attr) == 0) {
		i++;
	}
	if (mvpw_menu_get_item_attr(mythtv_browser, key, &item_attr) == 0) {
		uint32_t old_fg = item_attr.fg;
		item_attr.fg = mythtv_colors.livetv_current;
		mvpw_menu_set_item_attr(mythtv_browser, key, &item_attr);
		item_attr.fg = old_fg;
	}
	mvpw_menu_hilite_item(mythtv_browser, key);

 unlock:
	pthread_mutex_unlock(&myth_mutex);

 out:
	busy_end();
	ref_release(ctrl);
	ref_release(rec);
	ref_release(channame);
	changing_channel = 0;
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

static void
livetv_hilite_callback(mvp_widget_t *widget, char *item, void *key, bool hilite)
{
	int prog = (int)key;
	char buf[256];

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (hilite) {
		current_livetv = prog;
		snprintf(buf, sizeof(buf), "%s %s",
			 livetv_list[prog].pi[0].chan,
			 livetv_list[prog].pi[0].channame);
		mvpw_set_text_str(mythtv_channel, buf);
		mvpw_expose(mythtv_channel);
		snprintf(buf, sizeof(buf), "%s - %s",
			 livetv_list[prog].start, livetv_list[prog].end);
		mvpw_set_text_str(mythtv_date, buf);
		mvpw_expose(mythtv_date);
		mvpw_set_text_str(mythtv_description,
				  livetv_list[prog].description);
		mvpw_expose(mythtv_description);
	} else {
		mvpw_set_text_str(mythtv_channel, "");
		mvpw_expose(mythtv_channel);
		mvpw_set_text_str(mythtv_date, "");
		mvpw_expose(mythtv_date);
		mvpw_set_text_str(mythtv_description, "");
		mvpw_expose(mythtv_description);
	}
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

static int
get_livetv_programs_rec(int id, struct livetv_prog **list, int *n, int *p)
{
	cmyth_proginfo_t next_prog = NULL, cur = NULL;
	cmyth_conn_t ctrl = ref_hold(control);
	cmyth_recorder_t rec = ref_hold(mythtv_recorder);
	cmyth_timestamp_t ts;
	char *title = NULL, *subtitle = NULL, *channame = NULL;
	char *start_channame = NULL, *chansign = NULL;
	char *description = NULL;
	char *start, *end, *ptr;
	char tsbuf[256];
	int cur_id, i; 
	int shows = 0, unique = 0, busy = 0;
	struct livetv_proginfo *pi;
	
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	cur_id = cmyth_recorder_get_recorder_id(rec);
	
	
	fprintf(stderr,
		"Getting program listings for recorder %d [%d]\n",
		id, cur_id);

	if (cur_id != id) {
		ref_release(rec);
		rec = NULL;
		if ((rec = cmyth_conn_get_recorder_from_num(ctrl,
							    id)) == NULL) {
			fprintf(stderr,
				"failed to connect to tuner %d!\n", id);
			ref_release(ctrl);
			cmyth_dbg(CMYTH_DBG_DEBUG,
				    "%s [%s:%d]: (trace) -1}\n",
				    __FUNCTION__, __FILE__, __LINE__);
			return -1;
		}
	}

	if (cmyth_recorder_is_recording(rec) == 1)
		busy = 1;

	cur = cmyth_recorder_get_cur_proginfo(rec);
	cur = cmyth_recorder_get_next_proginfo(rec, cur, 1);
	if (cur == NULL) {
		int i;
		char channame[32];

		fprintf(stderr, "problem getting current proginfo!\n");

		/*
		 * mythbackend must not be tuned in to a channel, so keep
		 * changing channels until we find a valid one, or until
		 * we decide to give up.
		 */
		for (i=1; i<1000; i++) {
			snprintf(channame, sizeof(channame), "%d", i);
			if (cmyth_recorder_set_channel(rec, channame) < 0) {
				continue;
			}
			cur = cmyth_recorder_get_next_proginfo(rec, cur, 1);
			if (cur != NULL)
				break;
		}
	}
	if (cur == NULL) {
		fprintf(stderr, "get program info failed!\n");
		ref_release(rec);
		ref_release(ctrl);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			  __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}
	
	
	start_channame = (char *) cmyth_proginfo_channame(cur);
	do {
		next_prog = cmyth_recorder_get_next_proginfo(rec, cur, 1);
		if ( next_prog == NULL) {
			fprintf(stderr, "get next program info failed!\n");
			break;
		}

		title = (char *) cmyth_proginfo_title(next_prog);
		subtitle = (char *) cmyth_proginfo_subtitle(next_prog);
		description = (char *) cmyth_proginfo_description(next_prog);
		channame = (char *) cmyth_proginfo_channame(next_prog);
		chansign = (char *) cmyth_proginfo_chansign(next_prog);
						
		start = end = NULL;

		ts = cmyth_proginfo_start(next_prog);
		if (ts != NULL ) {
			cmyth_timestamp_to_string(tsbuf, ts);
			ref_release(ts);
			ptr = strchr(tsbuf, 'T');
			*ptr = '\0';
			memmove(tsbuf, ptr+1, strlen(ptr+1)+1);
			start = ref_strdup(tsbuf);
		}
		ts = cmyth_proginfo_end(next_prog);
		if (ts != NULL ) {
			cmyth_timestamp_to_string(tsbuf, ts);
			ref_release(ts);
			ptr = strchr(tsbuf, 'T');
			*ptr = '\0';
			memmove(tsbuf, ptr+1, strlen(ptr+1)+1);
			end = ref_strdup(tsbuf);
		}

		ref_release(cur);
		cur = ref_hold(next_prog);
		shows++;

		/*
		 * Search for duplicates only if the show has a title.
		 */
		if (title[0]) {
			for (i=0; i<*p; i++) {
				if ((strcmp((*list)[i].title, title) == 0) &&
				    (strcmp((*list)[i].subtitle, subtitle)
				     == 0) &&
				    (strcmp((*list)[i].description,
					    description) == 0) &&
				    (strcmp((*list)[i].start, start) == 0) &&
				    (strcmp((*list)[i].end, end) == 0)) {
					if ((*list)[i].count == MAX_TUNER)
						goto next;
					pi=&((*list)[i].pi[(*list)[i].count++]);
					pi->chan = ref_hold(channame);
					pi->channame = ref_hold(chansign);
					pi->rec_id = id;
					pi->busy = busy;
					goto next;
				}
			}
		}

		(*list)[*p].title = ref_hold(title);
		(*list)[*p].subtitle = ref_hold(subtitle);
		(*list)[*p].description = ref_hold(description);
		(*list)[*p].start = start;
		(*list)[*p].end = end;
		(*list)[*p].count = 1;
		(*list)[*p].pi[0].rec_id = id;
		(*list)[*p].pi[0].busy = busy;
		(*list)[*p].pi[0].chan = ref_hold(channame);
		(*list)[*p].pi[0].channame = ref_hold(chansign);
		(*p)++;
		unique++;

	next:
		if (*p == *n) {
			*n = *n*2;
			*list = realloc(*list, sizeof(**list)*(*n));
		}
		ref_release(title);
		ref_release(subtitle);
		ref_release(description);
		ref_release(channame);
		ref_release(chansign);
		ref_release(next_prog);
	} while (strcmp(start_channame, channame) != 0);

	ref_release(cur);
	ref_release(rec);
	ref_release(start_channame);
	fprintf(stderr, "Found %d shows on recorder %d (%d unique)\n",
		shows, id, unique);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);

	return shows;
}

static int
get_livetv_programs(void)
{
	struct livetv_prog *list;
	char buf[256];
	int i, j, c, n, p, found;
	time_t t;
	cmyth_conn_t ctrl = ref_hold(control);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (livetv_list) {
		for (i=0; i<livetv_count; i++) {
			ref_release(livetv_list[i].title);
			ref_release(livetv_list[i].subtitle);
			ref_release(livetv_list[i].description);
			ref_release(livetv_list[i].start);
			ref_release(livetv_list[i].end);
			for (j=0; j<livetv_list[i].count; j++) {
				ref_release(livetv_list[i].pi[j].chan);
				ref_release(livetv_list[i].pi[j].channame);
			}
		}
		free(livetv_list);
		livetv_count = 0;
		livetv_list = NULL;
	}

	n = 32;
	if ((list=(struct livetv_prog*)malloc(sizeof(*list)*n)) == NULL) {
		perror("malloc()");
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	if ((c=cmyth_conn_get_free_recorder_count(ctrl)) < 0) {
		fprintf(stderr, "unable to get free recorder\n");
		/* Sergio S. Commented out
		if (c == -2) {
			gui_error("LiveTV with this version of MythTV is not supported");
		}
		*/
		ref_release(ctrl);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -2}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	mvpw_clear_menu(mythtv_browser);

	item_attr.select = livetv_select_callback;
	item_attr.hilite = livetv_hilite_callback;

	p = 0;
	found = 0;
	for (i=0; i<MAX_TUNER; i++) {
		if (get_livetv_programs_rec(i+1, &list, &n, &p) != -1)
			found++;
	}

	t = time(NULL);
	fprintf(stderr, "Found %d programs on %d tuners at %s\n",
		p, found, ctime(&t));

	if (p == 0) {
		ref_release(ctrl);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	snprintf(buf, sizeof(buf), "Live TV - %d Programs on %d Tuner%s",
		 p, found, (found == 1) ? "" : "s");
	mvpw_set_menu_title(mythtv_browser, buf);

	qsort(list, p, sizeof(*list), livetv_compare);
	livetv_list = list;
	livetv_count = p;

	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;
	for (j=0; j<p; j++) {
		snprintf(buf, sizeof(buf), "%s: %s - %s",
			 list[j].pi[0].chan, list[j].title, list[j].subtitle);
		mvpw_add_menu_item(mythtv_browser, buf, (void*)j, &item_attr);
	}

	ref_release(ctrl);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return 0;
}

int
mythtv_new_livetv(void)
{
	int c, i;
	cmyth_recorder_t rec;
	cmyth_conn_t ctrl;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	/*
	 * Check that we can access the backend using mysql
	 * and if not, tell the user and give up.
	 */
	if(!mvp_tvguide_sql_check(mythtv_database)) {
		gui_error("You must have SQL access enabled on the server to use this feature");
		return -1;
	}

	fprintf(stderr, "Starting new livetv\n");

	switch_hw_state(MVPMC_STATE_MYTHTV);

	pthread_mutex_lock(&myth_mutex);
	ctrl = ref_hold(control);

	if ((c=cmyth_conn_get_free_recorder_count(ctrl)) < 0) {
		gui_error("No tuners available for Live TV.");
		fprintf(stderr, "unable to get free recorder\n");
		ref_release(ctrl);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -2}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	mvpw_clear_menu(mythtv_browser);

	/* Get the first available recorder for live tv */
	for (i=4; i<MAX_TUNER; i++) {
		if ((rec = cmyth_conn_get_recorder_from_num(ctrl, i+1)) != NULL) {
			if ( (cmyth_recorder_is_recording(rec) == 0) && (cmyth_tuner_type_check(mythtv_database,rec,mythtv_check_tuner_type))) 
				break;
			else
				ref_release(rec);
		}
	}

	if(i == MAX_TUNER) {
		gui_error("No tuners available for Live TV.");
		fprintf(stderr, "unable to get free recorder\n");
		ref_release(ctrl);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -2}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	pthread_mutex_unlock(&myth_mutex);
	/* Launch live tv on the free recorder */
	return mythtv_new_livetv_start(rec);
}

static int
mythtv_livetv_menu(void)
{
	int failed = 0;
	int err;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	fprintf(stderr, "Displaying livetv programs\n");

	pthread_mutex_lock(&myth_mutex);
	if ((err=get_livetv_programs()) < 0) {
		if (!mythtv_livetv) {
			if (err == -1)
				gui_error("No tuners available for Live TV.");
			failed = 1;
		}
	}
	pthread_mutex_unlock(&myth_mutex);

	if (!failed) {
		mvpw_show(mythtv_logo);
		mvpw_show(mythtv_browser);
		mvpw_focus(mythtv_browser);

		mvpw_show(mythtv_channel);
		mvpw_show(mythtv_date);
		mvpw_show(mythtv_description);
	}

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) %d}\n",
		    __FUNCTION__, __FILE__, __LINE__, failed);
	return failed;
}

int
mythtv_proginfo_livetv(char *buf, int size)
{
	snprintf(buf, size,
		 "Title: %s\n"
		 "Subtitle: %s\n"
		 "Description: %s\n"
		 "Start: %s\n"
		 "End: %s\n",
		 livetv_list[current_livetv].title,
		 livetv_list[current_livetv].subtitle,
		 livetv_list[current_livetv].description,
		 livetv_list[current_livetv].start,
		 livetv_list[current_livetv].end);

	return 0;
}

int
mythtv_livetv_tuners(int *tuners, int *busy)
{
	int i, n;

	n = livetv_list[current_livetv].count;

	for (i=0; i<n; i++) {
		tuners[i] = livetv_list[current_livetv].pi[i].rec_id;
		busy[i] = livetv_list[current_livetv].pi[i].busy;
	}

	return n;
}

void
mythtv_livetv_select(int which)
{
	cmyth_proginfo_t loc_prog = NULL;
	cmyth_conn_t ctrl = ref_hold(control);
	cmyth_recorder_t rec = ref_hold(mythtv_recorder);
	int rec_id = livetv_list[current_livetv].pi[which].rec_id;
	int tuner[2] = { rec_id, 0 };
	char *channame = ref_hold(livetv_list[current_livetv].pi[which].chan);
	

	switch_hw_state(MVPMC_STATE_MYTHTV);

	printf("starting liveTV on tuner %d channel %s index %d\n",
	       rec_id, channame, current_livetv);

	if (mythtv_livetv_start(tuner) != 0) {
		printf("livetv failed\n");
	} else {
		printf("livetv active, changing to channel %s\n", channame);

		changing_channel = 1;

		busy_start();
		pthread_mutex_lock(&myth_mutex);

		if (cmyth_recorder_pause(rec) < 0) {
			fprintf(stderr, "channel change (pause) failed\n");
			goto err;
		}

		if (cmyth_recorder_set_channel(rec, channame) < 0) {
			fprintf(stderr, "channel change failed!\n");
			goto err;
		}

		loc_prog = cmyth_recorder_get_cur_proginfo(rec);
		CHANGE_GLOBAL_REF(current_prog, loc_prog);
		ref_release(loc_prog);

		demux_reset(handle);
		demux_attr_reset(handle);
		av_play();
		video_play(root);

		add_osd_widget(mythtv_program_widget, OSD_PROGRAM,
			       osd_settings.program, NULL);
		mvpw_hide(mythtv_description);
		running_mythtv = 1;

		mythtv_fullscreen();

	err:
		pthread_mutex_unlock(&myth_mutex);
		busy_end();
		changing_channel = 0;
		ref_release(rec);
		ref_release(ctrl);
	}

	ref_release(channame);
}

int
mythtv_livetv_menu_start(void)
{
	int ver, rc;
	cmyth_conn_t ctrl;

	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	ctrl = ref_hold(control);
	ver = cmyth_conn_get_protocol_version(ctrl);
	ref_release(ctrl);

	if (ver >= 15) {
		rc = mythtv_new_livetv();
	} else {
		rc = mythtv_livetv_menu();
	}

	return rc;
}
