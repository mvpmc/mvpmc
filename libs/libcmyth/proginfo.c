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
 * proginfo.c - functions to manage MythTV program info.  This is
 *              information kept by MythTV to describe recordings and
 *              also to describe programs in the program guide.  The
 *              functions here allocate and fill out program
 *              information and lists of program information.  They
 *              also retrieve and manipulate recordings and program
 *              material based on program information.
 */
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <cmyth.h>
#include <cmyth_local.h>

/*
 * cmyth_proginfo_create(void)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Create a programinfo structure to be used to hold program
 * information and return a pointer to the structure.  The structure
 * is initialized to default values.
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_proginfo_t (this type is a pointer)
 *
 * Failure: A NULL cmyth_proginfo_t
 */
cmyth_proginfo_t
cmyth_proginfo_create(void)
{
	cmyth_proginfo_t ret;

	ret = malloc(sizeof(*ret));
	if (!ret) {
		return NULL;
	}
	cmyth_atomic_set(&ret->refcount, 1);
	ret->proginfo_start_ts = cmyth_timestamp_create();
	if (!ret->proginfo_start_ts) {
		goto err;
	}
	ret->proginfo_end_ts = cmyth_timestamp_create();
	if (!ret->proginfo_end_ts) {
		goto err;
	}
	ret->proginfo_rec_start_ts = cmyth_timestamp_create();
	if (!ret->proginfo_rec_start_ts) {
		goto err;
	}
	ret->proginfo_rec_end_ts = cmyth_timestamp_create();
	if (!ret->proginfo_rec_end_ts) {
		goto err;
	}
	ret->proginfo_lastmodified = cmyth_timestamp_create();
	if (!ret->proginfo_lastmodified) {
		goto err;
	}
	ret->proginfo_originalairdate = cmyth_timestamp_create();
	if (!ret->proginfo_originalairdate) {
		goto err;
	}
	ret->proginfo_title = NULL;
	ret->proginfo_subtitle = NULL;
	ret->proginfo_description = NULL;
	ret->proginfo_category = NULL;
	ret->proginfo_chanId = 0;
	ret->proginfo_chanstr = NULL;
	ret->proginfo_chansign = NULL;
	ret->proginfo_channame = NULL;
	ret->proginfo_chanicon = NULL;
	ret->proginfo_url = NULL;
	ret->proginfo_pathname = NULL;
	ret->proginfo_host = NULL;
	ret->proginfo_port = -1;
	ret->proginfo_Length = 0;
	ret->proginfo_conflicting = 0;
	ret->proginfo_unknown_0 = NULL;
	ret->proginfo_recording = 0;
	ret->proginfo_override = 0;
	ret->proginfo_hostname = NULL;
	ret->proginfo_source_id = 0;
	ret->proginfo_card_id = 0;
	ret->proginfo_input_id = 0;
	ret->proginfo_rec_priority = 0;
	ret->proginfo_rec_status = 0;
	ret->proginfo_record_id = 0;
	ret->proginfo_rec_type = 0;
	ret->proginfo_rec_dups = 0;
	ret->proginfo_unknown_1 = 0;
	ret->proginfo_repeat = 0;
	ret->proginfo_program_flags = 0;
	ret->proginfo_rec_profile = NULL;
	ret->proginfo_recgroup = NULL;
	ret->proginfo_chancommfree = NULL;
	ret->proginfo_chan_output_filters = NULL;
	ret->proginfo_seriesid = NULL;
	ret->proginfo_programid = NULL;
	ret->proginfo_stars = NULL;
	ret->proginfo_version = 12;
	return ret;

 err:
	cmyth_proginfo_release(ret);
	return NULL;

}

/*
 * cmyth_proginfo_destroy(cmyth_proginfo_t p)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Destroy the program info structure pointed to by 'p' and release
 * its storage.  This should only be called by
 * cmyth_proginfo_release(). All others should use
 * cmyth_proginfo_release() to release references to a program info
 * structure.
 *
 * Return Value:
 *
 * None.
 */
static void
cmyth_proginfo_destroy(cmyth_proginfo_t p)
{
	if (!p) {
		return;
	}
	if (p->proginfo_title) {
		free(p->proginfo_title);
	}
	if (p->proginfo_subtitle) {
		free(p->proginfo_subtitle);
	}
	if (p->proginfo_description) {
		free(p->proginfo_description);
	}
	if (p->proginfo_category) {
		free(p->proginfo_category);
	}
	if (p->proginfo_chanstr) {
		free(p->proginfo_chanstr);
	}
	if (p->proginfo_chansign) {
		free(p->proginfo_chansign);
	}
	if (p->proginfo_channame) {
		free(p->proginfo_channame);
	}
	if (p->proginfo_chanicon) {
		free(p->proginfo_chanicon);
	}
	if (p->proginfo_url) {
		free(p->proginfo_url);
	}
	if (p->proginfo_unknown_0) {
		free(p->proginfo_unknown_0);
	}
	if (p->proginfo_rec_priority) {
		free(p->proginfo_rec_priority);
	}
	if (p->proginfo_rec_profile) {
		free(p->proginfo_rec_profile);
	}
	if (p->proginfo_recgroup) {
		free(p->proginfo_recgroup);
	}
	if (p->proginfo_chan_output_filters) {
		free(p->proginfo_chan_output_filters);
	}
	if (p->proginfo_seriesid) {
		free(p->proginfo_seriesid);
	}
	if (p->proginfo_programid) {
		free(p->proginfo_programid);
	}
	if (p->proginfo_stars) {
		free(p->proginfo_stars);
	}
	if (p->proginfo_pathname) {
		free(p->proginfo_pathname);
	}
	if (p->proginfo_host) {
		free(p->proginfo_host);
	}
	if (p->proginfo_lastmodified) {
		cmyth_timestamp_release(p->proginfo_lastmodified);
	}
	if (p->proginfo_start_ts) {
		cmyth_timestamp_release(p->proginfo_start_ts);
	}
	if (p->proginfo_end_ts) {
		cmyth_timestamp_release(p->proginfo_end_ts);
	}
	if (p->proginfo_hostname) {
		free(p->proginfo_hostname);
	}
	if (p->proginfo_rec_start_ts) {
		cmyth_timestamp_release(p->proginfo_rec_start_ts);
	}
	if (p->proginfo_rec_end_ts) {
		cmyth_timestamp_release(p->proginfo_rec_end_ts);
	}
	if (p->proginfo_originalairdate) {
		cmyth_timestamp_release(p->proginfo_originalairdate);
	}
	if (p->proginfo_chancommfree) {
		free(p->proginfo_chancommfree);
	}

	memset(p, 0, sizeof(*p));

	free(p);
}

/*
 * cmyth_proginfo_hold(cmyth_proginfo_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Take a new reference to a proginfo structure.  Proginfo structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the proginfo will be
 * destroyed.  This function is how one creates a new holder of a
 * proginfo.  This function always returns the pointer passed to it.
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
cmyth_proginfo_t
cmyth_proginfo_hold(cmyth_proginfo_t p)
{
	if (p) {
		cmyth_atomic_inc(&p->refcount);
	}
	return p;
}

/*
 * cmyth_proginfo_release(cmyth_proginfo_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Release a reference to a proginfo structure.  Proginfo structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the proginfo will be
 * destroyed.  This function is how one drops a reference to a
 * proginfo.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_proginfo_release(cmyth_proginfo_t p)
{
	if (p) {
		if (cmyth_atomic_dec_and_test(&p->refcount)) {
			/*
			 * Last reference, free it.
			 */
			cmyth_proginfo_destroy(p);
		}
	}
}

/*
 * cmyth_proginfo_stop_recording(cmyth_conn_t control,
 *                               cmyth_proginfo_t prog)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Make a request on the control connection 'control' to ask the
 * MythTV back end to stop recording the program described in 'prog'.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
programinfo_stop_recording(cmyth_conn_t control,
						   cmyth_proginfo_t prog)
{
	return -ENOSYS;
}

/*
 * cmyth_proginfo_check_recording(cmyth_conn_t control,
 *                                cmyth_proginfo_t prog)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Make a request on the control connection 'control' to check the
 * existence of the program 'prog' on the MythTV back end.
 *
 * Return Value:
 *
 * Success: 1 - if the recording exists, 0 - if it does not
 *
 * Failure: -(ERRNO)
 */
int
programinfo_check_recording(cmyth_conn_t control,
							cmyth_proginfo_t prog)
{
	return -ENOSYS;
}

static int
delete_command(cmyth_conn_t control, cmyth_proginfo_t prog, char *cmd)
{
	long c = 0;
	char *buf;
	unsigned int len = ((2 * CMYTH_LONGLONG_LEN) + 
			    (4 * CMYTH_TIMESTAMP_LEN) +
			    (13 * CMYTH_LONG_LEN));
	char start_ts[CMYTH_TIMESTAMP_LEN + 1];
	char end_ts[CMYTH_TIMESTAMP_LEN + 1];
	char rec_start_ts[CMYTH_TIMESTAMP_LEN + 1];
	char rec_end_ts[CMYTH_TIMESTAMP_LEN + 1];
	char originalairdate[CMYTH_TIMESTAMP_LEN + 1];
	char lastmodified[CMYTH_TIMESTAMP_LEN + 1];
	char start_ts_dt[CMYTH_TIMESTAMP_LEN + 1];
	char end_ts_dt[CMYTH_TIMESTAMP_LEN + 1];
	char rec_start_ts_dt[CMYTH_TIMESTAMP_LEN + 1];
	char rec_end_ts_dt[CMYTH_TIMESTAMP_LEN + 1];
	char originalairdate_dt[CMYTH_TIMESTAMP_LEN + 1];
	char lastmodified_dt[CMYTH_TIMESTAMP_LEN + 1];
	int err;
	int count;
	long r;
	int ret;

	if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no program info\n",
			  __FUNCTION__);
		return -EINVAL;
	}

	len += strlen(prog->proginfo_title);
	len += strlen(prog->proginfo_subtitle);
	len += strlen(prog->proginfo_description);
	len += strlen(prog->proginfo_category);
	len += strlen(prog->proginfo_chanstr);
	len += strlen(prog->proginfo_chansign);
	len += strlen(prog->proginfo_channame);
	len += strlen(prog->proginfo_url);
	len += strlen(prog->proginfo_hostname);

	buf = alloca(len + 1+2048);
	if (!buf) {
		return -ENOMEM;
	}

	cmyth_timestamp_to_string(start_ts, prog->proginfo_start_ts);
	cmyth_timestamp_to_string(end_ts, prog->proginfo_end_ts);
	cmyth_timestamp_to_string(rec_start_ts, prog->proginfo_rec_start_ts);
	cmyth_timestamp_to_string(rec_end_ts, prog->proginfo_rec_end_ts);
	cmyth_timestamp_to_string(originalairdate, prog->proginfo_originalairdate);
	cmyth_timestamp_to_string(lastmodified, prog->proginfo_lastmodified);
	cmyth_datetime_to_string(start_ts_dt, prog->proginfo_start_ts);
	cmyth_datetime_to_string(end_ts_dt, prog->proginfo_end_ts);
	cmyth_datetime_to_string(rec_start_ts_dt, prog->proginfo_rec_start_ts);
	cmyth_datetime_to_string(rec_end_ts_dt, prog->proginfo_rec_end_ts);
	cmyth_datetime_to_string(originalairdate_dt, prog->proginfo_originalairdate);
	cmyth_datetime_to_string(lastmodified_dt, prog->proginfo_lastmodified);

	if (control->conn_version >= 14) {
		sprintf(buf,
			"%s 0[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%ld[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%lld[]:[]"
			"%lld[]:[]%s[]:[]%s[]:[]%s[]:[]%ld[]:[]"
			"%ld[]:[]%s[]:[]%ld[]:[]%ld[]:[]%ld[]:[]"
			"%s[]:[]%ld[]:[]%ld[]:[]%ld[]:[]%ld[]:[]"
			"%ld[]:[]%s[]:[]%s[]:[]%ld[]:[]%ld[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]",
			cmd,
			prog->proginfo_title,
			prog->proginfo_subtitle,
			prog->proginfo_description,
			prog->proginfo_category,
			prog->proginfo_chanId,
			prog->proginfo_chanstr,
			prog->proginfo_chansign,
			prog->proginfo_chanicon,
			prog->proginfo_url,
			prog->proginfo_Length >> 32,
			(prog->proginfo_Length & 0xffffffff),
			start_ts_dt,
			end_ts_dt,
			prog->proginfo_unknown_0,
			prog->proginfo_recording,
			prog->proginfo_override,
			prog->proginfo_hostname,
			prog->proginfo_source_id,
			prog->proginfo_card_id,
			prog->proginfo_input_id,
			prog->proginfo_rec_priority,
			prog->proginfo_rec_status,
			prog->proginfo_record_id,
			prog->proginfo_rec_type,
			prog->proginfo_rec_dups,
			prog->proginfo_unknown_1,
			rec_start_ts_dt,
			rec_end_ts_dt,
			prog->proginfo_repeat,
			prog->proginfo_program_flags,
			prog->proginfo_recgroup,
			prog->proginfo_chancommfree,
			prog->proginfo_chan_output_filters,
			prog->proginfo_seriesid,
			prog->proginfo_programid,
			lastmodified_dt,
			prog->proginfo_stars,
			originalairdate_dt); }
	else if (control->conn_version >= 12) {
		sprintf(buf,
			"%s 0[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%ld[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%lld[]:[]"
			"%lld[]:[]%s[]:[]%s[]:[]%s[]:[]%ld[]:[]"
			"%ld[]:[]%s[]:[]%ld[]:[]%ld[]:[]%ld[]:[]"
			"%s[]:[]%ld[]:[]%ld[]:[]%ld[]:[]%ld[]:[]"
			"%ld[]:[]%s[]:[]%s[]:[]%ld[]:[]%ld[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]",
			cmd,
			prog->proginfo_title,
			prog->proginfo_subtitle,
			prog->proginfo_description,
			prog->proginfo_category,
			prog->proginfo_chanId,
			prog->proginfo_chanstr,
			prog->proginfo_chansign,
			prog->proginfo_chanicon,
			prog->proginfo_url,
			prog->proginfo_Length >> 32,
			(prog->proginfo_Length & 0xffffffff),
			start_ts,
			end_ts,
			prog->proginfo_unknown_0,
			prog->proginfo_recording,
			prog->proginfo_override,
			prog->proginfo_hostname,
			prog->proginfo_source_id,
			prog->proginfo_card_id,
			prog->proginfo_input_id,
			prog->proginfo_rec_priority,
			prog->proginfo_rec_status,
			prog->proginfo_record_id,
			prog->proginfo_rec_type,
			prog->proginfo_rec_dups,
			prog->proginfo_unknown_1,
			rec_start_ts,
			rec_end_ts,
			prog->proginfo_repeat,
			prog->proginfo_program_flags,
			prog->proginfo_recgroup,
			prog->proginfo_chancommfree,
			prog->proginfo_chan_output_filters,
			prog->proginfo_seriesid,
			prog->proginfo_programid,
			lastmodified,
			prog->proginfo_stars,
			originalairdate);
	} else {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: delete not supported with protocol ver %d\n",
			  __FUNCTION__, control->conn_version);
		return -EINVAL;
	}

	pthread_mutex_lock(&mutex);

	if ((err = cmyth_send_message(control, buf)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		ret = err;
		goto out;
	}

	count = cmyth_rcv_length(control);
	if ((r=cmyth_rcv_long(control, &err, &c, count)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  __FUNCTION__, r);
		ret = err;
		goto out;
	}

	/*
	 * XXX: for some reason, this seems to return an error, even though
	 *      it succeeds...
	 */

	ret = 0;

 out:
	pthread_mutex_unlock(&mutex);

	return ret;
}

/*
 * cmyth_proginfo_delete_recording(cmyth_conn_t control,
 *                                 cmyth_proginfo_t prog)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Make a request on the control connection 'control' to delete the
 * program 'prog' from the MythTV back end.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_proginfo_delete_recording(cmyth_conn_t control, cmyth_proginfo_t prog)
{
	return delete_command(control, prog, "DELETE_RECORDING");
}

/*
 * cmyth_proginfo_forget_recording(cmyth_conn_t control,
 *                                 cmyth_proginfo_t prog)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Make a request on the control connection 'control' to tell the
 * MythTV back end to forget the program 'prog'.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_proginfo_forget_recording(cmyth_conn_t control, cmyth_proginfo_t prog)
{
	return delete_command(control, prog, "FORGET_RECORDING");
}

/*
 * cmyth_proginfo_get_recorder_num(cmyth_conn_t control,
 *                                 cmyth_rec_num_t rnum,
 *                                 cmyth_proginfo_t prog)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Make a request on the control connection 'control' to obtain the
 * recorder number for the program 'prog' and fill out 'rnum' with the
 * information.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_proginfo_get_recorder_num(cmyth_conn_t control,
								cmyth_rec_num_t rnum,
								cmyth_proginfo_t prog)
{
	return -ENOSYS;
}

/*
 * cmyth_proginfo_chan_id(cmyth_proginfo_t prog)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Obtain the channel identifier from a program information structure.
 *
 * Return Value:
 *
 * Success: A positive integer channel identifier (these are formed from
 *          the recorder number and the channel number).
 *
 * Failure: -(ERRNO)
 */
long
cmyth_proginfo_chan_id(cmyth_proginfo_t prog)
{
	if (!prog) {
		return -EINVAL;
	}
	return prog->proginfo_chanId;
}

/*
 * cmyth_proginfo_string(cmyth_proginfo_t prog)
 *
 *
 * Scope: PRIVATE (mapped to __cmyth_proginfo_string)
 *
 * Description
 *
 * Translates a cmyth_proginfo_t into a MythTV protocol string for
 * use in making requests.
 *
 * Return Value:
 *
 * Success: A pointer to a malloc'ed string.
 *
 * Failure: NULL
 */
char *
cmyth_proginfo_string(cmyth_proginfo_t prog)
{
	unsigned len = ((2 * CMYTH_LONGLONG_LEN) + 
					(4 * CMYTH_TIMESTAMP_LEN) +
					(13 * CMYTH_LONG_LEN));
	char start_ts[CMYTH_TIMESTAMP_LEN + 1];
	char end_ts[CMYTH_TIMESTAMP_LEN + 1];
	char rec_start_ts[CMYTH_TIMESTAMP_LEN + 1];
	char rec_end_ts[CMYTH_TIMESTAMP_LEN + 1];
	char originalairdate[CMYTH_TIMESTAMP_LEN + 1];
	char lastmodified[CMYTH_TIMESTAMP_LEN + 1];
	char *ret;

	len += strlen(prog->proginfo_title);
	len += strlen(prog->proginfo_subtitle);
	len += strlen(prog->proginfo_description);
	len += strlen(prog->proginfo_category);
	len += strlen(prog->proginfo_chanstr);
	len += strlen(prog->proginfo_chansign);
	len += strlen(prog->proginfo_channame);
	len += strlen(prog->proginfo_url);
	len += strlen(prog->proginfo_hostname);

	ret = malloc(len + 1+2048);
	if (!ret) {
		return NULL;
	}
	cmyth_timestamp_to_string(start_ts, prog->proginfo_start_ts);
	cmyth_timestamp_to_string(end_ts, prog->proginfo_end_ts);
	cmyth_timestamp_to_string(rec_start_ts, prog->proginfo_rec_start_ts);
	cmyth_timestamp_to_string(rec_end_ts, prog->proginfo_rec_end_ts);
	cmyth_timestamp_to_string(originalairdate, prog->proginfo_originalairdate);
	cmyth_timestamp_to_string(lastmodified, prog->proginfo_lastmodified);
	if (prog->proginfo_version >= 13) {
		sprintf(ret,
				"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%ld[]:[]"
				"%s[]:[]%s[]:[]%s[]:[]%s[]:[]"
				"%lld[]:[]%lld[]:[]%s[]:[]%s[]:[]%s[]:[]"
				"%ld[]:[]%ld[]:[]%s[]:[]%ld[]:[]%ld[]:[]"
				"%ld[]:[]%s[]:[]%ld[]:[]%ld[]:[]%ld[]:[]"
				"%ld[]:[]%ld[]:[]%s[]:[]%s[]:[]%ld[]:[]"
				"%ld[]:[]%s[]:[]%s[]:[]%s[]:[]"
				"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%s[]:[]",
				prog->proginfo_title,
				prog->proginfo_subtitle,
				prog->proginfo_description,
				prog->proginfo_category,
				prog->proginfo_chanId,
				prog->proginfo_chanstr,
				prog->proginfo_chansign,
				prog->proginfo_chanicon,
				prog->proginfo_url,
				prog->proginfo_Length >> 32,
				(prog->proginfo_Length & 0xffffffff),
				start_ts,
				end_ts,
				prog->proginfo_unknown_0,
				prog->proginfo_recording,
				prog->proginfo_override,
				prog->proginfo_hostname,
				prog->proginfo_source_id,
				prog->proginfo_card_id,
				prog->proginfo_input_id,
				prog->proginfo_rec_priority,
				prog->proginfo_rec_status,
				prog->proginfo_record_id,
				prog->proginfo_rec_type,
				prog->proginfo_rec_dups,
				prog->proginfo_unknown_1,
				rec_start_ts,
				rec_end_ts,
				prog->proginfo_repeat,
				prog->proginfo_program_flags,
				prog->proginfo_recgroup,
				prog->proginfo_chancommfree,
				prog->proginfo_chan_output_filters,
				prog->proginfo_seriesid,
				prog->proginfo_programid,
				lastmodified,
				prog->proginfo_stars,
				originalairdate);
	} else if (prog->proginfo_version >= 8) {
		sprintf(ret,
				"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%ld[]:[]"
				"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%lld[]:[]"
				"%lld[]:[]%s[]:[]%s[]:[]%s[]:[]%ld[]:[]"
				"%ld[]:[]%s[]:[]%ld[]:[]%ld[]:[]%ld[]:[]"
				"%s[]:[]%ld[]:[]%ld[]:[]%ld[]:[]%ld[]:[]"
				"%ld[]:[]%s[]:[]%s[]:[]%ld[]:[]%ld[]:[]"
				"%s[]:[]%s[]:[]%s[]:[]%s[]:[]",
				prog->proginfo_title,
				prog->proginfo_subtitle,
				prog->proginfo_description,
				prog->proginfo_category,
				prog->proginfo_chanId,
				prog->proginfo_chanstr,
				prog->proginfo_chansign,
				prog->proginfo_chanicon,
				prog->proginfo_url,
				prog->proginfo_Length >> 32,
				(prog->proginfo_Length & 0xffffffff),
				start_ts,
				end_ts,
				prog->proginfo_unknown_0,
				prog->proginfo_recording,
				prog->proginfo_override,
				prog->proginfo_hostname,
				prog->proginfo_source_id,
				prog->proginfo_card_id,
				prog->proginfo_input_id,
				prog->proginfo_rec_priority,
				prog->proginfo_rec_status,
				prog->proginfo_record_id,
				prog->proginfo_rec_type,
				prog->proginfo_rec_dups,
				prog->proginfo_unknown_1,
				rec_start_ts,
				rec_end_ts,
				prog->proginfo_repeat,
				prog->proginfo_program_flags,
				prog->proginfo_recgroup,
				prog->proginfo_chancommfree,
				prog->proginfo_chan_output_filters,
				prog->proginfo_seriesid);
	} else { /* Assume Version 1 */
		sprintf(ret,
				"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%ld[]:[]"
				"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%lld[]:[]"
				"%lld[]:[]%s[]:[]%s[]:[]%ld[]:[]%ld[]:[]"
				"%ld[]:[]%s[]:[]%ld[]:[]%ld[]:[]%ld[]:[]"
				"%s[]:[]%ld[]:[]%ld[]:[]%ld[]:[]%ld[]:[]"
				"%s[]:[]%s[]:[]%ld[]:[]%ld",
				prog->proginfo_title,
				prog->proginfo_subtitle,
				prog->proginfo_description,
				prog->proginfo_category,
				prog->proginfo_chanId,
				prog->proginfo_chanstr,
				prog->proginfo_chansign,
				prog->proginfo_channame,
				prog->proginfo_url,
				prog->proginfo_Length >> 32,
				(prog->proginfo_Length & 0xffffffff),
				start_ts,
				end_ts,
				prog->proginfo_conflicting,
				prog->proginfo_recording,
				prog->proginfo_override,
				prog->proginfo_hostname,
				prog->proginfo_source_id,
				prog->proginfo_card_id,
				prog->proginfo_input_id,
				prog->proginfo_rec_priority,
				prog->proginfo_rec_status,
				prog->proginfo_record_id,
				prog->proginfo_rec_type,
				prog->proginfo_rec_dups,
				rec_start_ts,
				rec_end_ts,
				prog->proginfo_repeat,
				prog->proginfo_program_flags);
	}
	return ret;
}

/*
 * cmyth_chaninfo_string(cmyth_proginfo_t prog)
 *
 *
 * Scope: PRIVATE (mapped to __cmyth_chaninfo_string)
 *
 * Description
 *
 * Translates a cmyth_proginfo_t into a MythTV protocol string for
 * use in making requests.
 *
 * Return Value:
 *
 * Success: A pointer to a malloc'ed string.
 *
 * Failure: NULL
 */
char *
cmyth_chaninfo_string(cmyth_proginfo_t prog)
{
	unsigned len = 2 * CMYTH_TIMESTAMP_LEN + CMYTH_LONG_LEN;
	char start_ts[CMYTH_TIMESTAMP_LEN + 1];
	char end_ts[CMYTH_TIMESTAMP_LEN + 1];
	char *ret;

	len += strlen(prog->proginfo_title);
	len += strlen(prog->proginfo_subtitle);
	len += strlen(prog->proginfo_description);
	len += strlen(prog->proginfo_category);
	len += strlen(prog->proginfo_chanstr);
	len += strlen(prog->proginfo_chansign);
	len += strlen(prog->proginfo_channame);
	len += strlen(prog->proginfo_url);

	ret = malloc(len + 1 +2048);
	if (!ret) {
		return NULL;
	}
	cmyth_timestamp_to_string(start_ts, prog->proginfo_start_ts);
	cmyth_timestamp_to_string(end_ts, prog->proginfo_end_ts);
	sprintf(ret,
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%s[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%s[]:[]%ld",
			prog->proginfo_title,
			prog->proginfo_subtitle,
			prog->proginfo_description,
			prog->proginfo_category,
			start_ts,
			end_ts,
			prog->proginfo_chansign,
			prog->proginfo_url,
			prog->proginfo_channame,
			prog->proginfo_chanstr,
			prog->proginfo_chanId);
	return ret;
}

/*
 * cmyth_proginfo_title(cmyth_proginfo_t prog)
 *
 *
 * Scope: PUBLIC
 *
 * Description
 *
 * Retrieves the 'proginfo_title' field of a program info structure.
 *
 * The returned string is a pointer to the string within the program
 * info structure, so it should not be modified by the caller.  The
 * return value is a 'const char *' for this reason.
 *
 * Return Value:
 *
 * Success: A pointer to a 'const char *' pointing to the field.
 *
 * Failure: NULL
 */
const char *
cmyth_proginfo_title(cmyth_proginfo_t prog)
{
    if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL program information\n",
					__FUNCTION__);
		return NULL;
	}
	return prog->proginfo_title;
}

/*
 * cmyth_proginfo_subtitle(cmyth_proginfo_t prog)
 *
 *
 * Scope: PUBLIC
 *
 * Description
 *
 * Retrieves the 'proginfo_subtitle' field of a program info structure.
 *
 * The returned string is a pointer to the string within the program
 * info structure, so it should not be modified by the caller.  The
 * return value is a 'const char *' for this reason.
 *
 * Return Value:
 *
 * Success: A pointer to a 'const char *' pointing to the field.
 *
 * Failure: NULL
 */
const char *
cmyth_proginfo_subtitle(cmyth_proginfo_t prog)
{
    if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL program information\n",
					__FUNCTION__);
		return NULL;
	}
	return prog->proginfo_subtitle;
}

/*
 * cmyth_proginfo_description(cmyth_proginfo_t prog)
 *
 *
 * Scope: PUBLIC
 *
 * Description
 *
 * Retrieves the 'proginfo_description' field of a program info structure.
 *
 * The returned string is a pointer to the string within the program
 * info structure, so it should not be modified by the caller.  The
 * return value is a 'const char *' for this reason.
 *
 * Return Value:
 *
 * Success: A pointer to a 'const char *' pointing to the field.
 *
 * Failure: NULL
 */
const char *
cmyth_proginfo_description(cmyth_proginfo_t prog)
{
    if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL program information\n",
					__FUNCTION__);
		return NULL;
	}
	return prog->proginfo_description;
}

/*
 * cmyth_proginfo_category(cmyth_proginfo_t prog)
 *
 *
 * Scope: PUBLIC
 *
 * Description
 *
 * Retrieves the 'proginfo_category' field of a program info structure.
 *
 * The returned string is a pointer to the string within the program
 * info structure, so it should not be modified by the caller.  The
 * return value is a 'const char *' for this reason.
 *
 * Return Value:
 *
 * Success: A pointer to a 'const char *' pointing to the field.
 *
 * Failure: NULL
 */
const char *
cmyth_proginfo_category(cmyth_proginfo_t prog)
{
    if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL program information\n",
					__FUNCTION__);
		return NULL;
	}
	return prog->proginfo_category;
}

const char *
cmyth_proginfo_seriesid(cmyth_proginfo_t prog)
{
    if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL series ID\n",
					__FUNCTION__);
		return NULL;
	}
	return prog->proginfo_seriesid;
}

const char *
cmyth_proginfo_programid(cmyth_proginfo_t prog)
{
    if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL program ID\n",
					__FUNCTION__);
		return NULL;
	}
	return prog->proginfo_programid;
}

const char *
cmyth_proginfo_stars(cmyth_proginfo_t prog)
{
    if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL stars\n",
					__FUNCTION__);
		return NULL;
	}
	return prog->proginfo_stars;
}

cmyth_timestamp_t
cmyth_proginfo_originalairdate(cmyth_proginfo_t prog)
{
    if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL original air date\n",
					__FUNCTION__);
		return NULL;
	}
	return cmyth_timestamp_hold(prog->proginfo_originalairdate);
}

/*
 * cmyth_proginfo_chanstr(cmyth_proginfo_t prog)
 *
 *
 * Scope: PUBLIC
 *
 * Description
 *
 * Retrieves the 'proginfo_chanstr' field of a program info structure.
 *
 * The returned string is a pointer to the string within the program
 * info structure, so it should not be modified by the caller.  The
 * return value is a 'const char *' for this reason.
 *
 * Return Value:
 *
 * Success: A pointer to a 'const char *' pointing to the field.
 *
 * Failure: NULL
 */
const char *
cmyth_proginfo_chanstr(cmyth_proginfo_t prog)
{
    if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL program information\n",
					__FUNCTION__);
		return NULL;
	}
	return prog->proginfo_chanstr;
}

/*
 * cmyth_proginfo_chansign(cmyth_proginfo_t prog)
 *
 *
 * Scope: PUBLIC
 *
 * Description
 *
 * Retrieves the 'proginfo_chansign' field of a program info
 * structure.
 *
 * The returned string is a pointer to the string within the program
 * info structure, so it should not be modified by the caller.  The
 * return value is a 'const char *' for this reason.
 *
 * Return Value:
 *
 * Success: A pointer to a 'const char *' pointing to the field.
 *
 * Failure: NULL
 */
const char *
cmyth_proginfo_chansign(cmyth_proginfo_t prog)
{
    if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL program information\n",
					__FUNCTION__);
		return NULL;
	}
	return prog->proginfo_chansign;
}

/*
 * cmyth_proginfo_channame(cmyth_proginfo_t prog)
 *
 *
 * Scope: PUBLIC
 *
 * Description
 *
 * Retrieves the 'proginfo_channame' field of a program info
 * structure.
 *
 * The returned string is a pointer to the string within the program
 * info structure, so it should not be modified by the caller.  The
 * return value is a 'const char *' for this reason.
 *
 * Return Value:
 *
 * Success: A pointer to a 'const char *' pointing to the field.
 *
 * Failure: NULL
 */
const char *
cmyth_proginfo_channame(cmyth_proginfo_t prog)
{
    if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL program information\n",
					__FUNCTION__);
		return NULL;
	}
	return prog->proginfo_channame;
}

/*
 * cmyth_proginfo_channame(cmyth_proginfo_t prog)
 *
 *
 * Scope: PUBLIC
 *
 * Description
 *
 * Retrieves the 'proginfo_pathname' field of a program info
 * structure.
 *
 * The returned string is a pointer to the string within the program
 * info structure, so it should not be modified by the caller.  The
 * return value is a 'const char *' for this reason.
 *
 * Return Value:
 *
 * Success: A pointer to a 'const char *' pointing to the field.
 *
 * Failure: NULL
 */
const char *
cmyth_proginfo_pathname(cmyth_proginfo_t prog)
{
	if (!prog) {
		return NULL;
	}
	return prog->proginfo_pathname;
}

/*
 * cmyth_proginfo_length(cmyth_proginfo_t prog)
 *
 *
 * Scope: PUBLIC
 *
 * Description
 *
 * Retrieves the 'proginfo_Length' field of a program info
 * structure.
 *
 * Return Value:
 *
 * Success: long long file length
 *
 * Failure: NULL
 */
long long
cmyth_proginfo_length(cmyth_proginfo_t prog)
{
	if (!prog) {
		return -1;
	}
	return prog->proginfo_Length;
}

/*
 * cmyth_proginfo_rec_start(cmyth_proginfo_t prog)
 *
 *
 * Scope: PUBLIC
 *
 * Description
 *
 * Retrieves the 'rec_start' timestamp from a program info structure.
 * This tells when a recording started.
 *
 * The returned timestamp is returned held.  It should be released
 * when no longer needed using cmyth_timestamp_release().
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_timestamp_t
 *
 * Failure: NULL
 */
cmyth_timestamp_t
cmyth_proginfo_rec_start(cmyth_proginfo_t prog)
{
	if (!prog) {
		return NULL;
	}
	return cmyth_timestamp_hold(prog->proginfo_rec_start_ts);
}


/*
 * cmyth_proginfo_rec_end(cmyth_proginfo_t prog)
 *
 *
 * Scope: PUBLIC
 *
 * Description
 *
 * Retrieves the 'rec_end' timestamp from a program info structure.
 * This tells when a recording started.
 *
 * The returned timestamp is returned held.  It should be released when no longer needed using cmyth_timestamp_release().
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_timestamp_t
 *
 * Failure: NULL
 */
cmyth_timestamp_t
cmyth_proginfo_rec_end(cmyth_proginfo_t prog)
{
	if (!prog) {
		return NULL;
	}
	return cmyth_timestamp_hold(prog->proginfo_rec_end_ts);
}

int
cmyth_proginfo_rec_status(cmyth_proginfo_t prog)
{
	if (!prog) {
		return 0;
	}
	return prog->proginfo_rec_status;
}

static int
fill_command(cmyth_conn_t control, cmyth_proginfo_t prog, char *cmd)
{
	char *buf;
	unsigned int len = ((2 * CMYTH_LONGLONG_LEN) + 
			    (4 * CMYTH_TIMESTAMP_LEN) +
			    (13 * CMYTH_LONG_LEN));
	char start_ts[CMYTH_TIMESTAMP_LEN + 1];
	char end_ts[CMYTH_TIMESTAMP_LEN + 1];
	char rec_start_ts[CMYTH_TIMESTAMP_LEN + 1];
	char rec_end_ts[CMYTH_TIMESTAMP_LEN + 1];
	char originalairdate[CMYTH_TIMESTAMP_LEN + 1];
	char lastmodified[CMYTH_TIMESTAMP_LEN + 1];
	char start_ts_dt[CMYTH_TIMESTAMP_LEN + 1];
	char end_ts_dt[CMYTH_TIMESTAMP_LEN + 1];
	char rec_start_ts_dt[CMYTH_TIMESTAMP_LEN + 1];
	char rec_end_ts_dt[CMYTH_TIMESTAMP_LEN + 1];
	char originalairdate_dt[CMYTH_TIMESTAMP_LEN + 1];
	char lastmodified_dt[CMYTH_TIMESTAMP_LEN + 1];
	int err;
	int ret;
	char *host = "mediamvp";

	if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no program info\n",
			  __FUNCTION__);
		return -EINVAL;
	}

	len += strlen(prog->proginfo_title);
	len += strlen(prog->proginfo_subtitle);
	len += strlen(prog->proginfo_description);
	len += strlen(prog->proginfo_category);
	len += strlen(prog->proginfo_chanstr);
	len += strlen(prog->proginfo_chansign);
	len += strlen(prog->proginfo_channame);
	len += strlen(prog->proginfo_url);
	len += strlen(prog->proginfo_hostname);

	buf = alloca(len + 1+2048);
	if (!buf) {
		return -ENOMEM;
	}

	cmyth_timestamp_to_string(start_ts, prog->proginfo_start_ts);
	cmyth_timestamp_to_string(end_ts, prog->proginfo_end_ts);
	cmyth_timestamp_to_string(rec_start_ts, prog->proginfo_rec_start_ts);
	cmyth_timestamp_to_string(rec_end_ts, prog->proginfo_rec_end_ts);
	cmyth_timestamp_to_string(originalairdate, prog->proginfo_originalairdate);
	cmyth_timestamp_to_string(lastmodified, prog->proginfo_lastmodified);
	cmyth_datetime_to_string(start_ts_dt, prog->proginfo_start_ts);
	cmyth_datetime_to_string(end_ts_dt, prog->proginfo_end_ts);
	cmyth_datetime_to_string(rec_start_ts_dt, prog->proginfo_rec_start_ts);
	cmyth_datetime_to_string(rec_end_ts_dt, prog->proginfo_rec_end_ts);
	cmyth_datetime_to_string(originalairdate_dt, prog->proginfo_originalairdate);
	cmyth_datetime_to_string(lastmodified_dt, prog->proginfo_lastmodified);

	if (control->conn_version >= 14) {
		sprintf(buf,
			"%s %s[]:[]0[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%ld[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%lld[]:[]"
			"%lld[]:[]%s[]:[]%s[]:[]%s[]:[]%ld[]:[]"
			"%ld[]:[]%s[]:[]%ld[]:[]%ld[]:[]%ld[]:[]"
			"%s[]:[]%ld[]:[]%ld[]:[]%ld[]:[]%ld[]:[]"
			"%ld[]:[]%s[]:[]%s[]:[]%ld[]:[]%ld[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]",
			cmd, host,
			prog->proginfo_title,
			prog->proginfo_subtitle,
			prog->proginfo_description,
			prog->proginfo_category,
			prog->proginfo_chanId,
			prog->proginfo_chanstr,
			prog->proginfo_chansign,
			prog->proginfo_chanicon,
			prog->proginfo_url,
			prog->proginfo_Length >> 32,
			(prog->proginfo_Length & 0xffffffff),
			start_ts_dt,
			end_ts_dt,
			prog->proginfo_unknown_0,
			prog->proginfo_recording,
			prog->proginfo_override,
			prog->proginfo_hostname,
			prog->proginfo_source_id,
			prog->proginfo_card_id,
			prog->proginfo_input_id,
			prog->proginfo_rec_priority,
			prog->proginfo_rec_status,
			prog->proginfo_record_id,
			prog->proginfo_rec_type,
			prog->proginfo_rec_dups,
			prog->proginfo_unknown_1,
			rec_start_ts_dt,
			rec_end_ts_dt,
			prog->proginfo_repeat,
			prog->proginfo_program_flags,
			prog->proginfo_recgroup,
			prog->proginfo_chancommfree,
			prog->proginfo_chan_output_filters,
			prog->proginfo_seriesid,
			prog->proginfo_programid,
			lastmodified_dt,
			prog->proginfo_stars,
			originalairdate_dt);
	} else if (control->conn_version >= 12) {
		sprintf(buf,
			"%s %s[]:[]0[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%ld[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]%lld[]:[]"
			"%lld[]:[]%s[]:[]%s[]:[]%s[]:[]%ld[]:[]"
			"%ld[]:[]%s[]:[]%ld[]:[]%ld[]:[]%ld[]:[]"
			"%s[]:[]%ld[]:[]%ld[]:[]%ld[]:[]%ld[]:[]"
			"%ld[]:[]%s[]:[]%s[]:[]%ld[]:[]%ld[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]"
			"%s[]:[]%s[]:[]%s[]:[]%s[]:[]",
			cmd, host,
			prog->proginfo_title,
			prog->proginfo_subtitle,
			prog->proginfo_description,
			prog->proginfo_category,
			prog->proginfo_chanId,
			prog->proginfo_chanstr,
			prog->proginfo_chansign,
			prog->proginfo_chanicon,
			prog->proginfo_url,
			prog->proginfo_Length >> 32,
			(prog->proginfo_Length & 0xffffffff),
			start_ts,
			end_ts,
			prog->proginfo_unknown_0,
			prog->proginfo_recording,
			prog->proginfo_override,
			prog->proginfo_hostname,
			prog->proginfo_source_id,
			prog->proginfo_card_id,
			prog->proginfo_input_id,
			prog->proginfo_rec_priority,
			prog->proginfo_rec_status,
			prog->proginfo_record_id,
			prog->proginfo_rec_type,
			prog->proginfo_rec_dups,
			prog->proginfo_unknown_1,
			rec_start_ts,
			rec_end_ts,
			prog->proginfo_repeat,
			prog->proginfo_program_flags,
			prog->proginfo_recgroup,
			prog->proginfo_chancommfree,
			prog->proginfo_chan_output_filters,
			prog->proginfo_seriesid,
			prog->proginfo_programid,
			lastmodified,
			prog->proginfo_stars,
			originalairdate);
	} else {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: fill not supported with protocol ver %d\n",
			  __FUNCTION__, control->conn_version);
		return -EINVAL;
	}

	if ((err = cmyth_send_message(control, buf)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		ret = err;
		goto out;
	}

	/*
	 * XXX: for some reason, this seems to return an error, even though
	 *      it succeeds...
	 */

	ret = 0;

 out:
	return ret;
}

int
cmyth_proginfo_fill(cmyth_conn_t control, cmyth_proginfo_t prog)
{
	int err = 0;
	int count;
	int ret;

	if (!control) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n", __FUNCTION__);
		return -EINVAL;
	}
	if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no program info\n", __FUNCTION__);
		return -EINVAL;
	}

	pthread_mutex_lock(&mutex);

	if ((ret=fill_command(control, prog, "FILL_PROGRAM_INFO") != 0))
		goto out;

	count = cmyth_rcv_length(control);
	if (count < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  __FUNCTION__, count);
		ret = count;
		goto out;
	}
	if (cmyth_rcv_proginfo(control, &err, prog, count) != count) {
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

int
cmyth_proginfo_compare(cmyth_proginfo_t a, cmyth_proginfo_t b)
{
	if (a == b)
		return 0;

	if ((a == NULL) || (b == NULL))
		return -1;

#define STRCMP(a, b) ( (a && b && (strcmp(a,b) == 0)) ? 0 : \
		       ((a == NULL) && (b == NULL) ? 0 : -1) )

	if (STRCMP(a->proginfo_title, b->proginfo_title) != 0)
		return -1;
	if (STRCMP(a->proginfo_subtitle, b->proginfo_subtitle) != 0)
		return -1;
	if (STRCMP(a->proginfo_description, b->proginfo_description) != 0)
		return -1;

	if (STRCMP(a->proginfo_url, b->proginfo_url) != 0)
		return -1;

	if (cmyth_timestamp_compare(a->proginfo_start_ts,
				    b->proginfo_start_ts) != 0)
		return -1;
	if (cmyth_timestamp_compare(a->proginfo_end_ts,
				    b->proginfo_end_ts) != 0)
		return -1;

	return 0;
}
