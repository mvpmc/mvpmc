/*
 *  Copyright (C) 2004, Eric Lund
 *  http://mvpmc.sourceforge.net/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ident "$Id$"

/*
 * recorder.c -   functions to handle operations on MythTV recorders.  A
 *                MythTV Recorder is the part of the MythTV backend that
 *                handles capturing video from a video capture card and
 *                storing it in files for transfer to a backend.  A
 *                recorder is the key to live-tv streams as well, and
 *                owns the tuner and channel information (i.e. program
 *                guide data).
 */
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <cmyth.h>
#include <cmyth_local.h>

/*
 * cmyth_recorder_create(void)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Allocate and initialize a cmyth recorder structure.
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_recorder_t (this type is a pointer)
 *
 * Failure: A NULL cmyth_recorder_t
 */
cmyth_recorder_t
cmyth_recorder_create(void)
{
	cmyth_recorder_t ret = malloc(sizeof *ret);

	if (!ret) {
		return NULL;
	}

	ret->rec_server = NULL;
	ret->rec_port = 0;
	ret->rec_have_stream = 0;
	ret->rec_id = 0;
	ret->rec_ring = NULL;
	ret->rec_conn = NULL;
	ret->rec_framerate = 0.0;
	cmyth_atomic_set(&ret->refcount, 1);
	return ret;
}

/*
 * cmyth_recorder_destroy(cmyth_recorder_t rec)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Clean up and free a recorder structure.  This should only be done
 * by the cmyth_recorder_release() code.  Everyone else should call
 * cmyth_recorder_release() because recorder structures are reference
 * counted.
 *
 * Return Value:
 *
 * None.
 */
static void
cmyth_recorder_destroy(cmyth_recorder_t rec)
{
	if (!rec) {
		return;
	}

	if (rec->rec_server) {
		free(rec->rec_server);
	}
	if (rec->rec_ring) {
		cmyth_ringbuf_release(rec->rec_ring);
	}
	if (rec->rec_conn) {
		cmyth_conn_release(rec->rec_conn);
	}
	memset(rec, 0, sizeof(rec));
	free(rec);
}

/*
 * cmyth_recorder_hold(cmyth_recorder_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Take a new reference to a recorder structure.  Recorder structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the recorder will be
 * destroyed.  This function is how one creates a new holder of a
 * recorder.  This function always returns the pointer passed to it.
 * While it cannot fail, if it is passed a NULL pointer, it will do
 * nothing.
 *
 * Return Value:
 *
 * Success: The value of 'p'
 *
 * Failure: There is no real failure case, but a NULL 'p' will result in a
 *          NULL return.
 */
cmyth_recorder_t
cmyth_recorder_hold(cmyth_recorder_t p)
{
	if (p) {
		cmyth_atomic_inc(&p->refcount);
	}
	return p;
}

/*
 * cmyth_recorder_release(cmyth_recorder_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Release a reference to a recorder structure.  Recorder structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the recorder will be
 * destroyed.  This function is how one drops a reference to a
 * recorder.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_recorder_release(cmyth_recorder_t p)
{
	if (p) {
		if (cmyth_atomic_dec_and_test(&p->refcount)) {
			/*
			 * Last reference, free it.
			 */
			cmyth_recorder_destroy(p);
		}
	}
}

/*
 * cmyth_recorder_request_block(cmyth_conn_t control,
 *                              cmyth_recorder_t rec,
 *                              unsigned len)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control channel 'control', request a block of 'len' bytes
 * of data from the recorder 'rec' on the backend server.  This begins
 * the flow of data on the data channel of the recorder.  Use
 * 'cmyth_conn_check_block()' to check whether the block has been
 * completely transferred.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_request_block(cmyth_conn_t control,
			     cmyth_recorder_t rec,
			     unsigned len)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_is_recording(cmyth_recorder_t rec)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control channel 'control', determine whether recorder 'rec' is
 * currently recording.  Return the true / false answer.
 *
 * Return Value:
 *
 * Success: 0 if the recorder is idle, 1 if the recorder is recording.
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_is_recording(cmyth_recorder_t rec)
{
	int err, count;
	int r;
	long c, ret;
	char msg[256];

	if (!rec) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no recorder connection\n",
			  __FUNCTION__);
		return -EINVAL;
	}

	pthread_mutex_lock(&mutex);

	snprintf(msg, sizeof(msg), "QUERY_RECORDER %u[]:[]IS_RECORDING",
		 rec->rec_id);

	if ((err=cmyth_send_message(rec->rec_conn, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		ret = err;
		goto out;
	}

	count = cmyth_rcv_length(rec->rec_conn);
	if ((r=cmyth_rcv_long(rec->rec_conn, &err, &c, count)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  __FUNCTION__, r);
		ret = err;
		goto out;
	}

	ret = c;

    out:
	pthread_mutex_unlock(&mutex);

	return ret;
}

/*
 * cmyth_recorder_get_framerate(
 *                              cmyth_recorder_t rec,
 *                              double *rate)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control channel 'control', obtain the current frame rate
 * for recorder 'rec'.  Put the result in 'rate'.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_get_framerate(cmyth_recorder_t rec,
			     double *rate)
{
	int err, count;
	int r;
	long ret;
	char msg[256];
	char reply[256];

	if (!rec || !rate) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no recorder connection\n",
			  __FUNCTION__);
		return -EINVAL;
	}

	pthread_mutex_lock(&mutex);

	snprintf(msg, sizeof(msg), "QUERY_RECORDER %u[]:[]GET_FRAMERATE",
		 rec->rec_id);

	if ((err=cmyth_send_message(rec->rec_conn, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		ret = err;
		goto out;
	}

	count = cmyth_rcv_length(rec->rec_conn);
	if ((r=cmyth_rcv_string(rec->rec_conn, &err,
				reply, sizeof(reply), count)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_string() failed (%d)\n",
			  __FUNCTION__, r);
		ret = err;
		goto out;
	}

	*rate = atof(reply);
	ret = 0;

    out:
	pthread_mutex_unlock(&mutex);

	return ret;
}

/*
 * cmyth_recorder_get_frames_written(cmyth_conn_t control,
 *                                   cmyth_recorder_t rec,
 *                                   double *rate)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control channel 'control', obtain the current number of
 * frames written by recorder 'rec'.
 *
 * Return Value:
 *
 * Success: long long number of frames (>= 0)
 *
 * Failure: long long -(ERRNO)
 */
long long
cmyth_recorder_get_frames_written(cmyth_conn_t control, cmyth_recorder_t rec)
{
	return (long long) -ENOSYS;
}

/*
 * cmyth_recorder_get_free_space(cmyth_conn_t control, cmyth_recorder_t rec)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control channel 'control', obtain the current number of
 * bytes of free space on the recorder 'rec'.
 *
 * Return Value:
 *
 * Success: long long freespace (>= 0)
 *
 * Failure: long long -(ERRNO)
 */
long long
cmyth_recorder_get_free_space(cmyth_conn_t control, cmyth_recorder_t rec)
{
	return (long long) -ENOSYS;
}

/*
 * cmyth_recorder_get_key_frame(cmyth_conn_t control,
 *                              cmyth_recorder_t rec,
 *                              long keynum)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control channel 'control', obtain the position in a
 * recording of the key frame number 'keynum' on the recorder 'rec'.
 *
 * Return Value:
 *
 * Success: long long seek offset >= 0
 *
 * Failure: long long -(ERRNO)
 */
long long
cmyth_recorder_get_keyframe_pos(cmyth_conn_t control,
				cmyth_recorder_t rec,
				unsigned long keynum)
{
	return (long long)-ENOSYS;
}

/*
 * cmyth_recorder_fill_position_map(cmyth_conn_t control,
 *                                  cmyth_recorder_t rec,
 *                                  cmyth_posmap_t map,
 *                                  long start,
 *                                  long end)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Request from the server connected as 'control' a list of {keynum,
 * position} pairs starting at keynum 'start' and ending with keynum
 * 'end' from the current recording on recorder 'rec'.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_fill_position_map(cmyth_conn_t control,
				 cmyth_recorder_t rec,
				 unsigned long start,
				 unsigned long end)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_get_recording(cmyth_conn_t control,
 *                              cmyth_recorder_t rec,
 *                              cmyth_proginfo_t proginfo)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Obtain from the server specified by 'control' and the recorder
 * specified by 'rec' program information for the current recording
 * (i.e. the recording being made right now), and put the program
 * information in 'proginfo'.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_get_recording(cmyth_conn_t control,
			     cmyth_recorder_t rec,
			     cmyth_proginfo_t proginfo)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_stop_playing(cmyth_conn_t control, cmyth_recorder_t rec)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', request that the recorder
 * 'rec' stop playing the current recording or live-tv stream.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_stop_playing(cmyth_conn_t control, cmyth_recorder_t rec)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_frontend_ready(cmyth_conn_t control, cmyth_recorder_t rec)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', let the recorder
 * 'rec' know that the frontend is ready for data.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_frontend_ready(cmyth_conn_t control, cmyth_recorder_t rec)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_cancel_next_recording(cmyth_conn_t control,
 *                                      cmyth_recorder_t rec)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', request that the recorder
 * 'rec' cancel its next scheduled recording.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_cancel_next_recording(cmyth_conn_t control,
				     cmyth_recorder_t rec)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_pause(cmyth_recorder_t rec)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', request that the recorder
 * 'rec' pause in the middle of the current recording.  This will
 * prevent the recorder from transmitting any data until the recorder
 * is unpaused.  At this moment, it is not clear to me what will cause
 * an unpause.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_pause(cmyth_recorder_t rec)
{
	int ret;
	char Buffer[255];

	pthread_mutex_lock(&mutex);

	sprintf(Buffer, "QUERY_RECORDER %ld[]:[]PAUSE", (long) rec->rec_id);
	if ((ret=cmyth_send_message(rec->rec_conn, Buffer)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message('%s') failed\n",
			  __FUNCTION__, Buffer);
		goto err;
	}

	if ((ret=cmyth_rcv_okay(rec->rec_conn, "ok")) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_okay() failed\n",
			  __FUNCTION__);
		goto err;
	}

	ret = 0;

    err:
	pthread_mutex_unlock(&mutex);

	return ret;
}

/*
 * cmyth_recorder_finish_recording(cmyth_conn_t control, cmyth_recorder_t rec)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', request that the recorder
 * 'rec' stop recording the current recording / live-tv.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_finish_recording(cmyth_conn_t control,
				cmyth_recorder_t rec)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_toggle_channel_favorite(cmyth_conn_t control,
 *                                        cmyth_recorder_t rec)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', request that the recorder
 * 'rec' switch among the favorite channels.  Note that the recorder
 * must not be actively recording when this request is made or bad
 * things may happen to the server (i.e. it may segfault).
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_toggle_channel_favorite(cmyth_conn_t control,
				       cmyth_recorder_t rec)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_change_channel(
 *                               cmyth_recorder_t rec,
 *                               cmyth_channeldir_t direction)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', request that the recorder
 * 'rec' change channels in one of the followin
 *
 * CHANNEL_DIRECTION_UP       - Go up one channel in the listing
 * 
 * CHANNEL_DIRECTION_DOWN     - Go down one channel in the listing
 * 
 * CHANNEL_DIRECTION_FAVORITE - Go to the next favorite channel
 * 
 * CHANNEL_DIRECTION_SAME     - Stay on the same (current) channel
 *
 * Note that the recorder must not be actively recording when this
 * request is made or bad things may happen to the server (i.e. it may
 * segfault).
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_change_channel(cmyth_recorder_t rec,
			      cmyth_channeldir_t direction)
{
	int err;
	int ret = -1;
	char msg[256];

	if (!rec) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no recorder connection\n",
			  __FUNCTION__);
		return -ENOSYS;
	}

	pthread_mutex_lock(&mutex);

	snprintf(msg, sizeof(msg),
		 "QUERY_RECORDER %d[]:[]CHANGE_CHANNEL[]:[]%d",
		 rec->rec_id, direction);

	if ((err=cmyth_send_message(rec->rec_conn, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		goto fail;
	}

	if ((err=cmyth_rcv_okay(rec->rec_conn, "ok")) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_okay() failed (%d)\n",
			  __FUNCTION__, err);
		goto fail;
	}

	rec->rec_ring->file_pos = 0;

	ret = 0;

    fail:
	pthread_mutex_unlock(&mutex);

	return ret;
}

/*
 * cmyth_recorder_set_channel(
 *                            cmyth_recorder_t rec,
 *                            char *channame)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', request that the recorder
 * 'rec' change channels to the channel named 'channame'.
 *
 * Note that the recorder must not be actively recording when this
 * request is made or bad things may happen to the server (i.e. it may
 * segfault).
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_set_channel(cmyth_recorder_t rec,
			   char *channame)
{
	int err;
	int ret = -1;
	char msg[256];

	if (!rec) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no recorder connection\n",
			  __FUNCTION__);
		return -ENOSYS;
	}

	pthread_mutex_lock(&mutex);

	snprintf(msg, sizeof(msg),
		 "QUERY_RECORDER %d[]:[]SET_CHANNEL[]:[]%s",
		 rec->rec_id, channame);

	if ((err=cmyth_send_message(rec->rec_conn, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		goto fail;
	}

	if ((err=cmyth_rcv_okay(rec->rec_conn, "ok")) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_okay() failed (%d)\n",
			  __FUNCTION__, err);
		goto fail;
	}

	rec->rec_ring->file_pos = 0;

	ret = 0;

    fail:
	pthread_mutex_unlock(&mutex);

	return ret;
}

/*
 * cmyth_recorder_change_color(cmyth_conn_t control,
 *                             cmyth_recorder_t rec,
 *                             cmyth_adjdir_t direction)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', request that the recorder
 * 'rec' change the color saturation of the currently recording
 * channel (this setting is preserved in the recorder settings for
 * that channel).  The change is controlled by the value of
 * 'direction' which may be:
 *
 *  ADJ_DIRECTION_UP           - Change the value upward one step
 *
 *	ADJ_DIRECTION_DOWN         - Change the value downward one step
 *
 * This may be done while the recorder is recording.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_change_color(cmyth_conn_t control,
			    cmyth_recorder_t rec,
			    cmyth_adjdir_t direction)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_change_brightness(cmyth_conn_t control,
 *                                  cmyth_recorder_t rec,
 *                                  cmyth_adjdir_t direction)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', request that the recorder
 * 'rec' change the brightness (black level) of the currently
 * recording channel (this setting is preserved in the recorder
 * settings for that channel).  The change is controlled by the value
 * of 'direction' which may be:
 *
 *  ADJ_DIRECTION_UP           - Change the value upward one step
 *
 *	ADJ_DIRECTION_DOWN         - Change the value downward one step
 *
 * This may be done while the recorder is recording.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_change_brightness(cmyth_conn_t control,
				 cmyth_recorder_t rec,
				 cmyth_adjdir_t direction)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_change_contrast(cmyth_conn_t control,
 *                                cmyth_recorder_t rec,
 *                                cmyth_adjdir_t direction)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', request that the recorder
 * 'rec' change the contrast of the currently recording channel (this
 * setting is preserved in the recorder settings for that channel).
 * The change is controlled by the value of 'direction' which may be:
 *
 *  ADJ_DIRECTION_UP           - Change the value upward one step
 *
 *	ADJ_DIRECTION_DOWN         - Change the value downward one step
 *
 * This may be done while the recorder is recording.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_change_contrast(cmyth_conn_t control,
			       cmyth_recorder_t rec,
			       cmyth_adjdir_t direction)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_change_hue(cmyth_conn_t control,
 *                           cmyth_recorder_t rec,
 *                           cmyth_adjdir_t direction)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', request that the recorder
 * 'rec' change the hue (color balance) of the currently recording
 * channel (this setting is preserved in the recorder settings for
 * that channel).  The change is controlled by the value of
 * 'direction' which may be:
 *
 *  ADJ_DIRECTION_UP           - Change the value upward one step
 *
 *	ADJ_DIRECTION_DOWN         - Change the value downward one step
 *
 * This may be done while the recorder is recording.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_change_hue(cmyth_conn_t control,
			  cmyth_recorder_t rec,
			  cmyth_adjdir_t direction)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_check_channel(cmyth_conn_t control,
 *                              cmyth_recorder_t rec,
 *                              char *channame)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', request that the recorder
 * 'rec' check the validity of the channel specified by 'channame'.
 *
 * Return Value:
 *
 * Success: 1 - valid channel, 0 - invalid channel
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_check_channel(cmyth_conn_t control,
			     cmyth_recorder_t rec,
                             char *channame)
{
	int err;
	int ret = -1;
	char msg[256];

	if (!control || !rec) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no recorder connection\n",
			  __FUNCTION__);
		return -ENOSYS;
	}

	pthread_mutex_lock(&mutex);

	snprintf(msg, sizeof(msg),
		 "QUERY_RECORDER %d[]:[]CHECK_CHANNEL[]:[]%s",
		 rec->rec_id, channame);

	if ((err=cmyth_send_message(control, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		goto fail;
	}

	if ((err=cmyth_rcv_okay(control, "ok")) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_okay() failed (%d)\n",
			  __FUNCTION__, err);
		goto fail;
	}

	ret = 0;

    fail:
	pthread_mutex_unlock(&mutex);

	return ret;
}

/*
 * cmyth_recorder_check_channel_prefix(cmyth_conn_t control,
 *                                     cmyth_recorder_t rec,
 *                                     char *channame)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Using the control connection 'control', request that the recorder
 * 'rec' check the validity of the channel prefix specified by
 * 'channame'.
 *
 * Return Value:
 *
 * Success: 1 - valid channel, 0 - invalid channel
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_check_channel_prefix(cmyth_conn_t control,
				    cmyth_recorder_t rec,
				    char *channame)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_get_program_info(
 *                                 cmyth_recorder_t rec,
 *                                 cmyth_proginfo_t proginfo)
 *
 * Scope: PUBLIC
 *
 * Description:
 *
 * Using the control connection 'control', request program information
 * from the recorder 'rec' for the current program in the program
 * guide (i.e. current channel and time slot).
 *
 * The program information will be used to fill out 'proginfo'.
 *
 * This does not affect the current recording.
 *
 * Return Value:
 *
 * Success: 1 - valid channel, 0 - invalid channel
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_get_program_info(cmyth_recorder_t rec,
				cmyth_proginfo_t proginfo)
{
	int err, count;
	int ret = -ENOSYS;
	char msg[256];

	if (!rec) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no recorder connection\n",
			  __FUNCTION__);
		return -ENOSYS;
	}

	pthread_mutex_lock(&mutex);

	snprintf(msg, sizeof(msg), "QUERY_RECORDER %d[]:[]GET_PROGRAM_INFO",
		 rec->rec_id);

	if ((err=cmyth_send_message(rec->rec_conn, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		ret = err;
		goto out;
	}

	count = cmyth_rcv_length(rec->rec_conn);
	if (cmyth_rcv_chaninfo(rec->rec_conn, &err, proginfo, count) != count) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_proginfo() < count\n", __FUNCTION__);
		ret = err;
		goto out;
	}

	ret = 0;
 
    out:
	pthread_mutex_unlock(&mutex);

	return ret;
}

/*
 * cmyth_recorder_get_next_program_info(
 *									         cmyth_recorder_t rec,
 *                                           cmyth_proginfo_t proginfo,
 *                                           cmyth_browsedir_t direction)
 *
 * Scope: PUBLIC
 *
 * Description:
 *
 * Using the control connection 'control', request program information
 * from the recorder 'rec' for the next program in the program guide
 * from the current program (i.e. current channel and time slot) in
 * the direction specified by 'direction' which may have any of the
 * following values:
 *
 *     BROWSE_DIRECTION_SAME        - Stay in the same place
 *     BROWSE_DIRECTION_UP          - Move up one slot (down one channel)
 *     BROWSE_DIRECTION_DOWN        - Move down one slot (up one channel)
 *     BROWSE_DIRECTION_LEFT        - Move left one slot (down one time slot)
 *     BROWSE_DIRECTION_RIGHT       - Move right one slot (up one time slot)
 *     BROWSE_DIRECTION_FAVORITE    - Move to the next favorite slot
 *
 * The program information will be used to fill out 'proginfo'.
 *
 * This does not affect the current recording.
 *
 * Return Value:
 *
 * Success: 1 - valid channel, 0 - invalid channel
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_get_next_program_info(cmyth_recorder_t rec,
				     cmyth_proginfo_t cur_prog,
				     cmyth_proginfo_t next_prog,
				     cmyth_browsedir_t direction)
{
        int err, count;
        int ret = -ENOSYS;
        char msg[256];
        char title[256], subtitle[256], desc[256], category[256],
		callsign[256], iconpath[256],
		channelname[256], chanid[256], seriesid[256], programid[256];
	char date[256];
	struct tm *tm;
	time_t t;
	cmyth_conn_t control;

        if (!rec) {
                cmyth_dbg(CMYTH_DBG_ERROR, "%s: no recorder connection\n",
                          __FUNCTION__);
                return -ENOSYS;
        }

	control = rec->rec_conn;

        pthread_mutex_lock(&mutex);

	t = time(NULL);
	tm = localtime(&t);
	snprintf(date, sizeof(date), "%.4d%.2d%.2d%.2d%.2d%.2d",
		 tm->tm_year + 1900, tm->tm_mon + 1,
		 tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

        snprintf(msg, sizeof(msg), "QUERY_RECORDER %d[]:[]GET_NEXT_PROGRAM_INFO[]:[]%s[]:[]%ld[]:[]%i[]:[]%s",
                 rec->rec_id, cur_prog->proginfo_channame,
		 cur_prog->proginfo_chanId, direction, date);

        if ((err=cmyth_send_message(control, msg)) < 0) {
                cmyth_dbg(CMYTH_DBG_ERROR,
                          "%s: cmyth_send_message() failed (%d)\n",
                          __FUNCTION__, err);
                ret = err;
                goto out;
        }

        count = cmyth_rcv_length(control);

	count -= cmyth_rcv_string(control, &err,
				  title, sizeof(title), count);
	count -= cmyth_rcv_string(control, &err,
				  subtitle, sizeof(subtitle), count);
	count -= cmyth_rcv_string(control, &err,
				  desc, sizeof(desc), count);
	count -= cmyth_rcv_string(control, &err,
				  category, sizeof(category), count);
	count -= cmyth_rcv_timestamp(control, &err,
				     next_prog->proginfo_start_ts, count);
	count -= cmyth_rcv_timestamp(control, &err,
				     next_prog->proginfo_end_ts, count);
	count -= cmyth_rcv_string(control, &err,
				  callsign, sizeof(callsign), count);
	count -= cmyth_rcv_string(control, &err,
				  iconpath, sizeof(iconpath), count);
	count -= cmyth_rcv_string(control, &err,
				  channelname, sizeof(channelname), count);
	count -= cmyth_rcv_string(control, &err,
				  chanid, sizeof(chanid), count);
	if (control->conn_version >= 12) {
		count -= cmyth_rcv_string(control, &err,
					  seriesid, sizeof(seriesid), count);
		count -= cmyth_rcv_string(control, &err,
					  programid, sizeof(programid), count);
	}

	if (count != 0) {
		ret = -1;
		goto out;
	}

	next_prog->proginfo_title = strdup(title);
	next_prog->proginfo_subtitle = strdup(subtitle);
	next_prog->proginfo_description = strdup(desc);
	next_prog->proginfo_channame = strdup(channelname);
	next_prog->proginfo_chansign = strdup(callsign);
	
	next_prog->proginfo_chanId = atoi(chanid);

	cmyth_timestamp_hold(next_prog->proginfo_start_ts);
	cmyth_timestamp_hold(next_prog->proginfo_end_ts);

	ret = 0;
 
    out:
        pthread_mutex_unlock(&mutex);

        return ret;
}

/*
 * cmyth_recorder_get_input_name(cmyth_conn_t control, cmyth_recorder_t rec)
 *
 * Scope: PUBLIC
 *
 * Description:
 *
 * Using the control connection 'control', request the current input name
 * from the recorder 'rec'.
 *
 * The input name up to 'len' bytes will be placed in the user
 * supplied string buffer 'name' which must be large enough to hold
 * 'len' bytes.  If the name is greater than or equal to 'len' bytes
 * long, the first 'len' - 1 bytes will be placed in 'name' followed
 * by a '\0' terminator.
 *
 * This does not affect the current recording.
 *
 * Return Value:
 *
 * Success: 1 - valid channel, 0 - invalid channel
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_get_input_name(cmyth_conn_t control,
			      cmyth_recorder_t rec,
			      char *name,
			      unsigned len)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_seek(cmyth_conn_t control,
 *                     cmyth_recorder_t rec,
 *                     long long pos,
 *                     cmyth_whence_t whence,
 *                     long long curpos)
 *
 * Scope: PUBLIC
 *
 * Description:
 *
 * Using the control connection 'control', request the recorder 'rec'
 * to seek to the offset specified by 'pos' using the specifier
 * 'whence' to indicate how to perform the seek.  The value of
 * 'whence' may be:
 *
 *    WHENCE_SET - set the seek offset absolutely from the beginning
 *                 of the stream.
 *
 *    WHENCE_CUR - set the seek offset relatively from the current
 *                 offset (as specified in 'curpos').
 *
 *    WHENCE_END - set the seek offset absolutely from the end
 *                 of the stream.
 *
 *
 * Return Value:
 *
 * Success: long long current offset >= 0
 *
 * Failure: (long long) -(ERRNO)
 */
long long
cmyth_recorder_seek(cmyth_conn_t control,
		    cmyth_recorder_t rec,
                    long long pos,
		    cmyth_whence_t whence,
		    long long curpos)

{
	return (long long) -ENOSYS;
}

/*
 * cmyth_recorder_spawn_livetv(
 *                             cmyth_recorder_t rec)
 *
 * Scope: PUBLIC
 *
 * Description:
 *
 * Using the control connection 'control', request the recorder 'rec'
 * to start recording live-tv on its current channel.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_spawn_livetv(cmyth_recorder_t rec)
{
	int err;
	int ret = -1;
	char msg[256];

	if (!rec) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no recorder connection\n",
			  __FUNCTION__);
		return -ENOSYS;
	}

	pthread_mutex_lock(&mutex);

	snprintf(msg, sizeof(msg), "QUERY_RECORDER %d[]:[]SPAWN_LIVETV",
		 rec->rec_id);

	if ((err=cmyth_send_message(rec->rec_conn, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		goto fail;
	}

	if ((err=cmyth_rcv_okay(rec->rec_conn, "ok")) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_okay() failed (%d)\n",
			  __FUNCTION__, err);
		goto fail;
	}

	ret = 0;

    fail:
	pthread_mutex_unlock(&mutex);

	return ret;
}

int
cmyth_recorder_stop_livetv(cmyth_recorder_t rec)
{
	int err;
	int ret = -1;
	char msg[256];

	if (!rec) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no recorder connection\n",
			  __FUNCTION__);
		return -ENOSYS;
	}

	pthread_mutex_lock(&mutex);

	snprintf(msg, sizeof(msg), "QUERY_RECORDER %d[]:[]STOP_LIVETV",
		 rec->rec_id);

	if ((err=cmyth_send_message(rec->rec_conn, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		goto fail;
	}

	if ((err=cmyth_rcv_okay(rec->rec_conn, "ok")) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_okay() failed (%d)\n",
			  __FUNCTION__, err);
		goto fail;
	}

	ret = 0;

    fail:
	pthread_mutex_unlock(&mutex);

	return ret;
}

int
cmyth_recorder_done_ringbuf(cmyth_recorder_t rec)
{
	int err;
	int ret = -1;
	char msg[256];

	if (!rec) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no recorder connection\n",
			  __FUNCTION__);
		return -ENOSYS;
	}

	pthread_mutex_lock(&mutex);

	snprintf(msg, sizeof(msg), "QUERY_RECORDER %d[]:[]DONE_RINGBUF",
		 rec->rec_id);

	if ((err=cmyth_send_message(rec->rec_conn, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		goto fail;
	}

	if ((err=cmyth_rcv_okay(rec->rec_conn, "OK")) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_okay() failed (%d)\n",
			  __FUNCTION__, err);
		goto fail;
	}

	ret = 0;

    fail:
	pthread_mutex_unlock(&mutex);

	return ret;
}

/*
 * cmyth_recorder_start_stream(cmyth_conn_t control,
 *                             cmyth_recorder_t rec)
 *
 * Scope: PUBLIC
 *
 * Description:
 *
 * Using the control connection 'control', request the recorder 'rec'
 * to start a stream of the current recording (or live-tv).
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_start_stream(cmyth_conn_t control,
			    cmyth_recorder_t rec)
{
	return -ENOSYS;
}

/*
 * cmyth_recorder_end_stream(cmyth_conn_t control,
 *                           cmyth_recorder_t rec)
 *
 * Scope: PUBLIC
 *
 * Description:
 *
 * Using the control connection 'control', request the recorder 'rec'
 * to end a stream of the current recording (or live-tv).
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_recorder_end_stream(cmyth_conn_t control,
			  cmyth_recorder_t rec)
{
	return -ENOSYS;
}

char*
cmyth_recorder_get_filename(cmyth_recorder_t rec)
{
	static char buf[128];

	if (!rec) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no recorder connection\n",
			  __FUNCTION__);
		return NULL;
	}

	snprintf(buf, sizeof(buf), "ringbuf%d.nuv", rec->rec_id);

	return buf;
}

int
cmyth_recorder_get_recorder_id(cmyth_recorder_t rec)
{
	if (!rec) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no recorder connection\n",
			  __FUNCTION__);
		return -1;
	}

	return rec->rec_id;
}
