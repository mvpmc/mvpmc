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
 * freespace.c - functions to manage freespace structures.
 */
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <cmyth.h>
#include <cmyth_local.h>

/*
 * cmyth_freespace_create(void)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Create a key frame structure.
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_freespace_t (this type is a pointer)
 *
 * Failure: A NULL cmyth_freespace_t
 */
cmyth_freespace_t
cmyth_freespace_create(void)
{
	cmyth_freespace_t ret = malloc(sizeof(*ret));
	if (!ret) {
		return NULL;
	}

	ret->freespace_total = 0;
	ret->freespace_used = 0;
	cmyth_atomic_set(&ret->refcount, 1);
	return ret;
}

/*
 * cmyth_freespace_destroy(void)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Tear down and free a freespace structure.  This should only be
 * called by cmyth_freespace_release().  All others should call
 * cmyth_freespace_release() to release references to freespaces.
 *
 * Return Value:
 *
 * None.
 */
static void
cmyth_freespace_destroy(cmyth_freespace_t kf)
{
	if (!kf) {
		return;
	}
	free(kf);
}

/*
 * cmyth_freespace_hold(cmyth_freespace_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Take a new reference to a freespace structure.  Freespace structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the freespace will be
 * destroyed.  This function is how one creates a new holder of a
 * freespace.  This function always returns the pointer passed to it.
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
cmyth_freespace_t
cmyth_freespace_hold(cmyth_freespace_t p)
{
	if (p) {
		cmyth_atomic_inc(&p->refcount);
	}
	return p;
}

/*
 * cmyth_freespace_release(cmyth_freespace_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Release a reference to a freespace structure.  Freespace structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the freespace will be
 * destroyed.  This function is how one drops a reference to a
 * freespace.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_freespace_release(cmyth_freespace_t p)
{
	if (p) {
		if (cmyth_atomic_dec_and_test(&p->refcount)) {
			/*
			 * Last reference, free it.
			 */
			cmyth_freespace_destroy(p);
		}
	}
}
