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
 * ringbuf.c -    functions to handle operations on MythTV ringbuffers.  A
 *                MythTV Ringbuffer is the part of the backend that handles
 *                recording of live-tv for streaming to a MythTV frontend.
 *                This allows the watcher to do things like pause, rewind
 *                and so forth on live-tv.
 */
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <cmyth.h>
#include <cmyth_local.h>

/*
 * cmyth_ringbuf_create(void)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Allocate and initialize a ring buffer structure.
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_ringbuf_t (this type is a pointer)
 *
 * Failure: A NULL cmyth_ringbuf_t
 */
cmyth_ringbuf_t
cmyth_ringbuf_create(void)
{
	cmyth_ringbuf_t ret = malloc(sizeof *ret);

	if (!ret) {
		return NULL;
	}

	ret->ringbuf_url = NULL;
	ret->ringbuf_id = 0;
	ret->ringbuf_size = 0;
	ret->ringbuf_start = 0;
	ret->ringbuf_end = 0;
	cmyth_atomic_set(&ret->refcount, 1);
	return ret;
}

/*
 * cmyth_ringbuf_destroy(cmyth_ringbuf_t rb)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Clean up and free a ring buffer structure.  This should only be done
 * by the cmyth_ringbuf_release() code.  Everyone else should call
 * cmyth_ringbuf_release() because ring buffer structures are reference
 * counted.
 *
 * Return Value:
 *
 * None.
 */
static void
cmyth_ringbuf_destroy(cmyth_ringbuf_t rb)
{
	if (!rb) {
		return;
	}

	if (rb->ringbuf_url) {
		free(rb->ringbuf_url);
	}
	free(rb);
}

/*
 * cmyth_ringbuf_hold(cmyth_ringbuf_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Take a new reference to a ring buffer structure.  Ring Buffer structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the ring buffer will be
 * destroyed.  This function is how one creates a new holder of a
 * ring buffer.  This function always returns the pointer passed to it.
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
cmyth_ringbuf_t
cmyth_ringbuf_hold(cmyth_ringbuf_t p)
{
	if (p) {
		cmyth_atomic_inc(&p->refcount);
	}
	return p;
}

/*
 * cmyth_ringbuf_release(cmyth_ringbuf_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Release a reference to a ring buffer structure.  Ring Buffer structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the ring buffer will be
 * destroyed.  This function is how one drops a reference to a
 * ring buffer.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_ringbuf_release(cmyth_ringbuf_t p)
{
	if (p) {
		if (cmyth_atomic_dec_and_test(&p->refcount)) {
			/*
			 * Last reference, free it.
			 */
			cmyth_ringbuf_destroy(p);
		}
	}
}

