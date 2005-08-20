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
 * rec_num.c -  functions to manage recorder number structures.  Mostly
 *              just allocating, freeing, and filling them out.
 */
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <cmyth.h>
#include <cmyth_local.h>

/*
 * cmyth_rec_num_create(void)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Create a recorder number structure.
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_rec_num_t (this type is a pointer)
 *
 * Failure: A NULL cmyth_rec_num_t
 */
cmyth_rec_num_t
cmyth_rec_num_create(void)
{
	cmyth_rec_num_t ret = malloc(sizeof(*ret));

	if (!ret) {
		return NULL;
	}
	ret->recnum_host = NULL;
	ret->recnum_port = 0;
	ret->recnum_id = 0;
	cmyth_atomic_set(&ret->refcount, 1);
	return ret;
}

/*
 * cmyth_rec_num_destroy(cmyth_rec_num_t rn)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Destroy and release all storage associated with the recorder number
 * structure 'rn'.  This function should only ever be called by
 * cmyth_rec_num_release().  All others should call
 * cmyth_rec_num_release() to free rec_num structures.
 *
 * Return Value:
 *
 * None.
 */
static void
cmyth_rec_num_destroy(cmyth_rec_num_t rn)
{
	if (!rn) {
		return;
	}
	if (rn->recnum_host) {
		free(rn->recnum_host);
	}
}

/*
 * cmyth_rec_num_hold(cmyth_rec_num_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Take a new reference to a rec_num structure.  Rec_Num structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the rec_num will be
 * destroyed.  This function is how one creates a new holder of a
 * rec_num.  This function always returns the pointer passed to it.
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
cmyth_rec_num_t
cmyth_rec_num_hold(cmyth_rec_num_t p)
{
	if (p) {
		cmyth_atomic_inc(&p->refcount);
	}
	return p;
}

/*
 * cmyth_rec_num_release(cmyth_rec_num_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Release a reference to a rec_num structure.  Rec_Num structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the rec_num will be
 * destroyed.  This function is how one drops a reference to a
 * rec_num.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_rec_num_release(cmyth_rec_num_t p)
{
	if (p) {
		if (cmyth_atomic_dec_and_test(&p->refcount)) {
			/*
			 * Last reference, free it.
			 */
			cmyth_rec_num_destroy(p);
		}
	}
}

/*
 * cmyth_rec_num_fill(cmyth_rec_num_t rn,
 *                    char *host,
 *                    unsigned short port,
 *                    unsigned id)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Fill out the contents of the recorder number structure 'rn' using
 * the values 'host', 'port', and 'id'.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
void
cmyth_rec_num_fill(cmyth_rec_num_t rn,
		   char *host,
		   unsigned short port,
		   unsigned id)
{
	if (!rn) {
		return;
	}
	rn->recnum_host = strdup(host);
	if (!rn->recnum_host) {
		return;
	}
	rn->recnum_port = port;
	rn->recnum_id = id;
}

/*
 * cmyth_rec_num_string(cmyth_rec_num_t rn)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Compose a MythTV protocol string from a rec_num structure and
 * return a pointer to a malloc'ed buffer containing the string.
 *
 * Return Value:
 *
 * Success: A non-NULL malloc'ed character buffer pointer.
 *
 * Failure: NULL
 */
char *
cmyth_rec_num_string(cmyth_rec_num_t rn)
{
	unsigned len = sizeof("[]:[][]:[]");
	char id[16];
	char port[8];
	char *ret;

	if (!rn) {
		return NULL;
	}
	if (!rn->recnum_host) {
		return NULL;
	}
	sprintf(id, "%d", rn->recnum_id);
	len += strlen(id);
	sprintf(port, "%d", rn->recnum_port);
	len += strlen(port);
	len += strlen(rn->recnum_host);
	ret = malloc((len + 1) * sizeof(char));
	if (!ret) {
		return NULL;
	}
	strcpy(ret, id);
	strcat(ret, "[]:[]");
	strcat(ret, rn->recnum_host);
	strcat(ret, "[]:[]");
	strcat(ret, port);
	return ret;
}
