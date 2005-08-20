/*
 *  Copyright (C) 2005, Eric Lund
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
 * alloc.c -   Memory management functions.  The structures returned from
 *             libcmyth APIs are actually pointers to reference counted
 *             blocks of memory.  The functions provided here handle allocating
 *             these blocks (strictly internally to the library), placing
 *             holds on these blocks (publicly) and releasing holds (publicly).
 *
 *             All pointer type return values, including strings are reference
 *             counted.
 *
 *       NOTE: Since reference counted pointers are used to move
 *             these structures around, it is strictly forbidden to
 *             modify the contents of a structure once its pointer has
 *             been returned to a callerthrough an API function.  This
 *             means that all API functions that modify the contents
 *             of a structure must copy the structure, modify the
 *             copy, and return a pointer to the copy.  It is safe to
 *             copy pointers (as long as you hold them) everyone
 *             follows this simple rule.  There is no need for deep
 *             copying of any structure.
 */
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <cmyth.h>
#include <cmyth_local.h>

#include <string.h>

/*
 * struct refcounter
 *
 * Scope: PRIVATE (local to this module)
 *
 * Description:
 *
 * The structure used to manage references.  One of these is prepended to every
 * allocation.  It contains two key pieces of information:
 *
 * - The reference count on the structure or string attached to it
 *
 * - A pointer to a 'destroy' function (a destructor) to be called when
 *   the last reference is released.  This function facilitates tearing down
 *   of any complex structures contained in the reference counted block.  If
 *   it is NULL, no function is called.
 *
 * NOTE: Make sure this has a word aligned length, as it will be placed
 *       before each allocation and will affect the alignment of pointers.
 */
typedef struct refcounter {
	cmyth_atomic_t refcount;
	void (*destroy)(void *p);
} refcounter_t;

/*
 * cmyth_allocate(size_t len)
 * 
 * Scope: PRIVATE (mapped to __cmyth_allocate)
 *
 * Description
 *
 * Allocate a reference counted block of data for use as a libcmyth structure
 * or string.
 *
 * Return Value:
 *
 * Success: A non-NULL pointer to  a block of memory at least 'len' bytes long
 *          and safely aligned.  The block is reference counted and can be
 *          released using cmyth_release().
 *
 * Failure: A NULL pointer.
 */
void *
cmyth_allocate(size_t len)
{
	void *block = malloc(sizeof(refcounter_t) + len);
	void *ret = (((unsigned char *)block) + sizeof(refcounter_t));
	refcounter_t *ref = (refcounter_t *)block;

	if (ref) {
		cmyth_atomic_inc(&ref->refcount);
		ref->destroy = NULL;
		return ret;
	}
	return NULL;
}

/*
 * cmyth_alloc_set_destroy(void *block, void (*func)(*p))
 * 
 * Scope: PRIVATE (mapped to __cmyth_set_destroy)
 *
 * Description
 *
 * Set the destroy function for a block of data.  The first argument
 * is a pointer to the data block (as returned by cmyth_allocate()).  The
 * second argument is a pointer to the destroy function which, when
 * called, will be passed one argument, the pointer to the block (as
 * returned by cmyth_allocate()).  The destroy function is
 * respsonsible for any cleanup needed prior to finally releasing the
 * memory holding the memory block.
 *
 * Return Value: NONE
 */
void
cmyth_alloc_set_destroy(void *data, void (*func)(void *p))
{
	void *block = (((unsigned char *)data) - sizeof(refcounter_t));
	refcounter_t *ref = block;

	if (data) {
		ref->destroy = func;
	}
}

/*
 * cmyth_alloc_strdup(char *str)
 * 
 * Scope: PRIVATE (mapped to __cmyth_alloc_strdup)
 *
 * Description
 *
 * Similar to the libc version of strdup() except that it returns a pointer
 * to a reference counted string.
 *
 * Return Value: 
 *
 * Success: A non-NULL pointer to  a reference counted string which can be
 *          released using cmyth_release().
 *
 * Failure: A NULL pointer.
 */
char *
cmyth_alloc_strdup(char *str)
{
	size_t len = strlen(str) + 1;
	char *ret = cmyth_allocate(len);

	if (ret) {
		strncpy(ret, str, len);
		ret[len - 1] = '\0';
	}
	return ret;
}

/*
 * cmyth_hold(void *p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * This is how holders of references to reference counted blocks take
 * additional references.  The argument is a pointer to a structure or
 * string returned from a libcmyth API function (or from
 * cmyth_allocate).  The structure's reference count will be
 * incremented  and a  pointer to that space returned.
 *
 * There is really  no error condition possible, but if a NULL pointer
 * is passed in, a NULL is returned.
 *
 * NOTE: since this function operates outside of the space that is directly
 *       accessed by  the pointer, if a pointer that was NOT allocated by
 *       cmyth_allocate() is provided, negative consequences are likely.
 *
 * Return Value: A  pointer to the held space
 */
void *
cmyth_hold(void *p)
{
	void *block = (((unsigned char *)p) - sizeof(refcounter_t));
	refcounter_t *ref = block;

	if (p) {
		cmyth_atomic_inc(&ref->refcount);
		ref->destroy = NULL;
		return p;
	}
        return NULL;
}

/*
 * cmyth_release(void *p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * This is how holders of references to reference counted blocks release
 * those references.  The argument is a pointer to a structure or string
 * returned from a libcmyth API function (or from cmyth_allocate).  The
 * structure's reference count will be decremented and, when it reaches zero
 * the structure's destroy function (if any) will be called and then the
 * memory block will be released.
 *
 * Return Value: NONE
 */
void
cmyth_release(void *p)
{
	void *block = (((unsigned char *)p) - sizeof(refcounter_t));
	refcounter_t *ref = block;

	if (p) {
		if (cmyth_atomic_dec_and_test(&ref->refcount)) {
			/*
			 * Last reference, destroy the structure (if
			 * there is a destroy function) and free the
			 * block.
			 */
			if (ref->destroy) {
				ref->destroy(p);
			}
			free(block);
		}
	}
}
