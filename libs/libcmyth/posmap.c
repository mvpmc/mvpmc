/*
 * posmap.c - functions to handle operations on MythTV position maps.
 *            A position map contains a list of key-frames each of
 *            which represents a indexed position in a recording
 *            stream.  These may be markers set by hand, or they may
 *            be markers inserted by commercial detection.  A position
 *            map collects these in one place.
 */
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <cmyth.h>
#include <cmyth_local.h>

/*
 * cmyth_posmap_create(void)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Allocate and initialize a position map structure.
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_posmap_t (this type is a pointer)
 *
 * Failure: A NULL cmyth_posmap_t
 */
cmyth_posmap_t
cmyth_posmap_create(void)
{
	cmyth_posmap_t ret = malloc(sizeof *ret);

	if (!ret) {
		return NULL;
	}
	ret->posmap_count = 0;
	ret->posmap_list = NULL;
	cmyth_atomic_set(&ret->refcount, 1);
	return ret;
}

/*
 * cmyth_posmap_destroy(cmyth_posmap_t pm)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Clean up and free a position map structure.  This should only be done
 * by the cmyth_posmap_release() code.  Everyone else should call
 * cmyth_posmap_release() because position map structures are reference
 * counted.
 *
 * Return Value:
 *
 * None.
 */
static void
cmyth_posmap_destroy(cmyth_posmap_t pm)
{
	int i;
	if (!pm) {
		return;
	}
	if (pm->posmap_list) {
		for (i = 0; i < pm->posmap_count; ++i) {
			cmyth_keyframe_release(pm->posmap_list[i]);
		}
		free(pm->posmap_list);
	}
	free(pm);
}

/*
 * cmyth_posmap_hold(cmyth_posmap_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Take a new reference to a position map structure.  Position Map structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the position map will be
 * destroyed.  This function is how one creates a new holder of a
 * position map.  This function always returns the pointer passed to it.
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
cmyth_posmap_t
cmyth_posmap_hold(cmyth_posmap_t p)
{
	if (p) {
		cmyth_atomic_inc(&p->refcount);
	}
	return p;
}

/*
 * cmyth_posmap_release(cmyth_posmap_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Release a reference to a position map structure.  Position Map structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the position map will be
 * destroyed.  This function is how one drops a reference to a
 * position map.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_posmap_release(cmyth_posmap_t p)
{
	if (p) {
		if (cmyth_atomic_dec_and_test(&p->refcount)) {
			/*
			 * Last reference, free it.
			 */
			cmyth_posmap_destroy(p);
		}
	}
}
