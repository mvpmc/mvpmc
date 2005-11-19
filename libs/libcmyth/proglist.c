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
 * proglist.c - functions to manage MythTV timestamps.  Primarily,
 *               these allocate timestamps and convert between string
 *               and cmyth_proglist_t and between long long and
 *               cmyth_proglist_t.
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
 * cmyth_proglist_destroy(void)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Destroy and free a timestamp structure.  This should only be called
 * by cmyth_release().  All others should use
 * cmyth_release() to release references to time stamps.
 *
 * Return Value:
 *
 * None.
 */
static void
cmyth_proglist_destroy(cmyth_proglist_t pl)
{
	int i;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s\n", __FUNCTION__);
	if (!pl) {
		return;
	}
	for (i  = 0; i < pl->proglist_count; ++i) {
		if (pl->proglist_list[i]) {
			cmyth_release(pl->proglist_list[i]);
		}
		pl->proglist_list[i] = NULL;
	}
	if (pl->proglist_list) {
		free(pl->proglist_list);
	}
}

/*
 * cmyth_proglist_create(void)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Create a timestamp structure and return a pointer to the structure.
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_proglist_t (this type is a pointer)
 *
 * Failure: A NULL cmyth_proglist_t
 */
cmyth_proglist_t
cmyth_proglist_create(void)
{
	cmyth_proglist_t ret;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s\n", __FUNCTION__);
	ret = cmyth_allocate(sizeof(*ret));
	if (!ret) {
		return(NULL);
	}
	cmyth_set_destroy(ret, (destroy_t)cmyth_proglist_destroy);

	ret->proglist_list = NULL;
	ret->proglist_count = 0;
	return ret;
}

/*
 * cmyth_proglist_get_item(cmyth_proglist_t pl, int index)
 *
 * Scope: PUBLIC
 *
 * Description:
 *
 * Retrieve the program information structure found at index 'index'
 * in the list in 'pl'.  Return the program information structure
 * held.  Before forgetting the reference to this program info structure
 * the caller must call cmyth_release().
 *
 * Return Value:
 *
 * Success: A non-null cmyth_proginfo_t (this is a pointer type)
 *
 * Failure: A NULL cmyth_proginfo_t
 */
cmyth_proginfo_t
cmyth_proglist_get_item(cmyth_proglist_t pl, int index)
{
	if (!pl) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL program list\n",
			  __FUNCTION__);
		return NULL;
	}
	if (!pl->proglist_list) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL list\n",
			  __FUNCTION__);
		return NULL;
	}
	if ((index < 0) || (index >= pl->proglist_count)) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: index %d out of range\n",
			  __FUNCTION__, index);
		return NULL;
	}
	cmyth_hold(pl->proglist_list[index]);
	return pl->proglist_list[index];
}

int
cmyth_proglist_delete_item(cmyth_proglist_t pl, cmyth_proginfo_t prog)
{
	int i;
	cmyth_proginfo_t old;
	int ret = -EINVAL;

	if (!pl) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL program list\n",
			  __FUNCTION__);
		return -EINVAL;
	}
	if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL program item\n",
			  __FUNCTION__);
		return -EINVAL;
	}

	pthread_mutex_lock(&mutex);

	for (i=0; i<pl->proglist_count; i++) {
		if (cmyth_proginfo_compare(prog, pl->proglist_list[i]) == 0) {
			old = pl->proglist_list[i];
			memmove(pl->proglist_list+i,
				pl->proglist_list+i+1,
				(pl->proglist_count-i-1)*sizeof(cmyth_proginfo_t));
			pl->proglist_count--;
			cmyth_release(old);
			ret = 0;
			goto out;
		}
	}

 out:
	pthread_mutex_unlock(&mutex);

	return ret;
}

/*
 * cmyth_proglist_get_count(cmyth_proglist_t pl)
 *
 * Scope: PUBLIC
 *
 * Description:
 *
 * Retrieve the number of elements in the program information
 * structure in 'pl'.
 *
 * Return Value:
 *
 * Success: A number >= 0 indicating the number of items in 'pl'
 *
 * Failure: -errno
 */
int
cmyth_proglist_get_count(cmyth_proglist_t pl)
{
	if (!pl) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL program list\n",
			  __FUNCTION__);
		return -EINVAL;
	}
	return pl->proglist_count;
}

/*
 * cmyth_proglist_get_list(cmyth_conn_t conn,
 *                         cmyth_proglist_t proglist,
 *                         char *msg, char *func)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Obtain a program list from the query specified in 'msg' from the
 * function 'func'.  Make the query on 'conn' and put the results in
 * 'proglist'.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
static int
cmyth_proglist_get_list(cmyth_conn_t conn,
			cmyth_proglist_t proglist,
			char *msg, const char *func)
{
	int err = 0;
	int count;
	int ret;

	if (!conn) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n", func);
		return -EINVAL;
	}
	if (!proglist) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no program list\n", func);
		return -EINVAL;
	}

	pthread_mutex_lock(&mutex);

	if ((err = cmyth_send_message(conn, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  func, err);
		ret = err;
		goto out;
	}
	count = cmyth_rcv_length(conn);
	if (count < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  func, count);
		ret = count;
		goto out;
	}
	if (strcmp(msg, "QUERY_GETALLPENDING") == 0) {
		long c;
		int r;
		if ((r=cmyth_rcv_long(conn, &err, &c, count)) < 0) {
			cmyth_dbg(CMYTH_DBG_ERROR,
				  "%s: cmyth_rcv_length() failed (%d)\n",
				  __FUNCTION__, r);
			ret = err;
			goto out;
		}
		count -= r;
	}
	if (cmyth_rcv_proglist(conn, &err, proglist, count) != count) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_proglist() < count\n",
			  func);
	}
	if (err) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_proglist() failed (%d)\n",
			  func, err);
		ret = -1 * err;
		goto out;
	}

	ret = 0;

    out:
	pthread_mutex_unlock(&mutex);

	return ret;
}

/*
 * cmyth_proglist_get_all_recorded(cmyth_conn_t control,
 *                                 cmyth_proglist_t *proglist)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Make a request on the control connection 'control' to obtain a list
 * of completed or in-progress recordings.  Build a list of program
 * information structures and put a malloc'ed pointer to the list (an
 * array of pointers) in proglist.
 *
 * Return Value:
 *
 * Success: A held, noon-NULL cmyth_proglist_t
 *
 * Failure: NULL
 */
cmyth_proglist_t
cmyth_proglist_get_all_recorded(cmyth_conn_t control)
{
	cmyth_proglist_t proglist = cmyth_proglist_create();

	if (proglist == NULL) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_proglist_create() failed\n",
			  __FUNCTION__);
		return NULL;
	}

	if (cmyth_proglist_get_list(control, proglist,
				    "QUERY_RECORDINGS Play",
				    __FUNCTION__) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_proglist_get_list() failed\n",
			  __FUNCTION__);
		cmyth_release(proglist);
		return NULL;
	}
	return proglist;
}

/*
 * cmyth_proglist_get_all_pending(cmyth_conn_t control,
 *                                cmyth_proglist_t *proglist)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Make a request on the control connection 'control' to obtain a list
 * of pending recordings.  Build a list of program information
 * structures and put a malloc'ed pointer to the list (an array of
 * pointers) in proglist.
 *
 * Return Value:
 *
 * Success: A held, noon-NULL cmyth_proglist_t
 *
 * Failure: NULL
 */
cmyth_proglist_t
cmyth_proglist_get_all_pending(cmyth_conn_t control)
{
	cmyth_proglist_t proglist = cmyth_proglist_create();

	if (proglist == NULL) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_proglist_create() failed\n",
			  __FUNCTION__);
		return NULL;
	}

	if (cmyth_proglist_get_list(control, proglist,
				    "QUERY_GETALLPENDING",
				    __FUNCTION__) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_proglist_get_list() failed\n",
			  __FUNCTION__);
		cmyth_release(proglist);
		return NULL;
	}
	return proglist;
}

/*
 * cmyth_proglist_get_all_scheduled(cmyth_conn_t control,
 *                                  cmyth_proglist_t *proglist)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Make a request on the control connection 'control' to obtain a list
 * of scheduled recordings.  Build a list of program information
 * structures and put a malloc'ed pointer to the list (an array of
 * pointers) in proglist.
 *
 * Return Value:
 *
 * Success: A held, noon-NULL cmyth_proglist_t
 *
 * Failure: NULL
 */
cmyth_proglist_t
cmyth_proglist_get_all_scheduled(cmyth_conn_t control)
{
	cmyth_proglist_t proglist = cmyth_proglist_create();

	if (proglist == NULL) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_proglist_create() failed\n",
			  __FUNCTION__);
		return NULL;
	}

	if (cmyth_proglist_get_list(control, proglist,
				    "QUERY_GETALLSCHEDULED",
				    __FUNCTION__) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_proglist_get_list() failed\n",
			  __FUNCTION__);
		cmyth_release(proglist);
		return NULL;
	}
	return proglist;
}

/*
 * cmyth_proglist_get_conflicting(cmyth_conn_t control,
 *                                cmyth_proglist_t *proglist)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Make a request on the control connection 'control' to obtain a list
 * of conflicting recordings.  Build a list of program information
 * structures and put a malloc'ed pointer to the list (an array of
 * pointers) in proglist.
 *
 * Return Value:
 *
 * Success: A held, noon-NULL cmyth_proglist_t
 *
 * Failure: NULL
 */
cmyth_proglist_t
cmyth_proglist_get_conflicting(cmyth_conn_t control)
{
	cmyth_proglist_t proglist = cmyth_proglist_create();

	if (proglist == NULL) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_proglist_create() failed\n",
			  __FUNCTION__);
		return NULL;
	}

	if (cmyth_proglist_get_list(control, proglist,
				    "QUERY_GETCONFLICTING",
				    __FUNCTION__) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_proglist_get_list() failed\n",
			  __FUNCTION__);
		cmyth_release(proglist);
		return NULL;
	}
	return proglist;
}
