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
 * keyframe.c - functions to manage key frame structures.  Mostly
 *              just allocating, freeing, and filling them out.
 */
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <cmyth.h>
#include <cmyth_local.h>

/*
 * cmyth_keyframe_create(void)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Create a key frame structure.
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_keyframe_t (this type is a pointer)
 *
 * Failure: A NULL cmyth_keyframe_t
 */
cmyth_keyframe_t
cmyth_keyframe_create(void)
{
	cmyth_keyframe_t ret = malloc(sizeof(*ret));

	if (!ret) {
		return NULL;
	}
	ret->keyframe_number = 0;
	ret->keyframe_pos = 0;
	cmyth_atomic_set(&ret->refcount, 1);
	return ret;
}

/*
 * cmyth_keyframe_destroy(void)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Tear down and free a keyframe structure.  This should only be
 * called by cmyth_keyframe_release().  All others should call
 * cmyth_keyframe_release() to release references to keyframes.
 *
 * Return Value:
 *
 * None.
 */
static void
cmyth_keyframe_destroy(cmyth_keyframe_t kf)
{
	if (!kf) {
		return;
	}
	free(kf);
}

/*
 * cmyth_keyframe_hold(cmyth_keyframe_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Take a new reference to a keyframe structure.  Keyframe structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the keyframe will be
 * destroyed.  This function is how one creates a new holder of a
 * keyframe.  This function always returns the pointer passed to it.
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
cmyth_keyframe_t
cmyth_keyframe_hold(cmyth_keyframe_t p)
{
	if (p) {
		cmyth_atomic_inc(&p->refcount);
	}
	return p;
}

/*
 * cmyth_keyframe_release(cmyth_keyframe_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Release a reference to a keyframe structure.  Keyframe structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the keyframe will be
 * destroyed.  This function is how one drops a reference to a
 * keyframe.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_keyframe_release(cmyth_keyframe_t p)
{
	if (p) {
		if (cmyth_atomic_dec_and_test(&p->refcount)) {
			/*
			 * Last reference, free it.
			 */
			cmyth_keyframe_destroy(p);
		}
	}
}

/*
 * cmyth_keyframe_fill(cmyth_keyframe_t kf,
 * 					   unsigned long keynum,
 *					   unsigned long long pos)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Fill out the contents of the recorder number structure 'rn' using
 * the values 'keynum' and 'pos'.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
void
cmyth_keyframe_fill(cmyth_keyframe_t kf,
					unsigned long keynum,
					unsigned long long pos)
{
	if (!kf) {
		return;
	}

	kf->keyframe_number = keynum;
	kf->keyframe_pos = pos;
}

/*
 * cmyth_keyframe_string(cmyth_keyframe_t kf)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Compose a MythTV protocol string from a keyframe structure and
 * return a pointer to a malloc'ed buffer containing the string.
 *
 * Return Value:
 *
 * Success: A non-NULL malloc'ed character buffer pointer.
 *
 * Failure: NULL
 */
char *
cmyth_keyframe_string(cmyth_keyframe_t kf)
{
	unsigned len = sizeof("[]:[]");
	char key[32];
	char pos[32];
	char *ret;

	if (!kf) {
		return NULL;
	}
	sprintf(pos, "%lld", kf->keyframe_pos);
	len += strlen(pos);
	sprintf(key, "%ld", kf->keyframe_number);
	len += strlen(key);
	ret = malloc(len * sizeof(char));
	if (!ret) {
		return NULL;
	}
	strcpy(ret, key);
	strcat(ret, "[]:[]");
	strcat(ret, pos);
	return ret;
}
