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
#include <cmyth.h>
#include <cmyth_local.h>

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
	cmyth_proglist_t ret = malloc(sizeof(*ret));

	if (!ret) {
		return(NULL);
	}
	ret->proglist_list = NULL;
	ret->proglist_count = 0;
	cmyth_atomic_set(&ret->refcount, 1);
	return ret;
}

/*
 * cmyth_proglist_destroy(void)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Destroy and free a timestamp structure.  This should only be called
 * by cmyth_proglist_release().  All others should use
 * cmyth_proglist_release() to release references to time stamps.
 *
 * Return Value:
 *
 * None.
 */
static void
cmyth_proglist_destroy(cmyth_proglist_t pl)
{
	int i;

	if (!pl) {
		return;
	}
	for (i  = 0; i < pl->proglist_count; ++i) {
		if (pl->proglist_list[i]) {
			cmyth_proginfo_release(pl->proglist_list[i]);
		}
		pl->proglist_list[i] = NULL;
	}
	pl->proglist_count = 0;
	free(pl);
}

/*
 * cmyth_proglist_hold(cmyth_proglist_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Take a new reference to a timestamp structure.  Timestamp structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the timestamp will be
 * destroyed.  This function is how one creates a new holder of a
 * timestamp.  This function always returns the pointer passed to it.
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
cmyth_proglist_t
cmyth_proglist_hold(cmyth_proglist_t p)
{
	if (p) {
		cmyth_atomic_inc(&p->refcount);
	}
	return p;
}

/*
 * cmyth_proglist_release(cmyth_proglist_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Release a reference to a timestamp structure.  Timestamp structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the timestamp will be
 * destroyed.  This function is how one drops a reference to a
 * timestamp.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_proglist_release(cmyth_proglist_t p)
{
	if (p) {
		if (cmyth_atomic_dec_and_test(&p->refcount)) {
			/*
			 * Last reference, free it.
			 */
			cmyth_proglist_destroy(p);
		}
	}
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
 * the caller must call cmyth_proginfo_release().
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
	cmyth_proginfo_hold(pl->proglist_list[index]);
	return pl->proglist_list[index];
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
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL program list\n", __FUNCTION__);
		return -EINVAL;
	}
	return pl->proglist_count;
}

/*
 * cmyth_proglist_get_list(cmyth_conn_t conn,
 *							   cmyth_proglist_t proglist,
 *							   char *msg, char *func)
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
							char *msg, char *func)
{
	int err = 0;
	int count;

	if (!conn) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n", func);
		return -EINVAL;
	}
	if (!proglist) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no program list\n", func);
		return -EINVAL;
	}
	if ((err = cmyth_send_message(conn, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_send_message() failed (%d)\n",
				  func, err);
		return err;
	}
	count = cmyth_rcv_length(conn);
	if (count < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_length() failed (%d)\n",
				  func, count);
		return count;
	}
	if (cmyth_rcv_proglist(conn, &err, proglist, count) != count) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_proglist() < count\n",
				  func);
	}
	if (err) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_proglist() failed (%d)\n",
				  func, err);
		err = -1 * err;
		return err;
	}
	return 0;
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
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_proglist_get_all_recorded(cmyth_conn_t control,
							 cmyth_proglist_t proglist)
{
	return cmyth_proglist_get_list(control, proglist,
									   "QUERY_RECORDINGS Play", __FUNCTION__);
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
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_proglist_get_all_pending(cmyth_conn_t control,
							cmyth_proglist_t proglist)
{
	return cmyth_proglist_get_list(control, proglist,
									   "QUERY_GETALLPENDING", __FUNCTION__);
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
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_proglist_get_all_scheduled(cmyth_conn_t control,
							  cmyth_proglist_t proglist)
{
	return cmyth_proglist_get_list(control, proglist,
									   "QUERY_GETALLSCHEDULED", __FUNCTION__);
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
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_proglist_get_conflicting(cmyth_conn_t control,
							cmyth_proglist_t proglist)
{
	return cmyth_proglist_get_list(control, proglist,
									   "QUERY_GETCONFLICTING", __FUNCTION__);
}

