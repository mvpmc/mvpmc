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
	ret->proginfo_title = NULL;
	ret->proginfo_subtitle = NULL;
	ret->proginfo_description = NULL;
	ret->proginfo_category = NULL;
	ret->proginfo_chanId = 0;
	ret->proginfo_chanstr = NULL;
	ret->proginfo_chansign = NULL;
	ret->proginfo_channame = NULL;
	ret->proginfo_url = NULL;
	ret->proginfo_pathname = NULL;
	ret->proginfo_host = NULL;
	ret->proginfo_port = -1;
	ret->proginfo_Start = 0;
	ret->proginfo_Length = 0;
	ret->proginfo_conflicting = 0;
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
	ret->proginfo_repeat = 0;
	ret->proginfo_program_flags = 0;
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
	if (p->proginfo_url) {
		free(p->proginfo_url);
	}
	if (p->proginfo_pathname) {
		free(p->proginfo_pathname);
	}
	if (p->proginfo_host) {
		free(p->proginfo_host);
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
programinfo_delete_recording(cmyth_conn_t control,
							 cmyth_proginfo_t prog)
{
	return -ENOSYS;
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
programinfo_forget_recording(cmyth_conn_t control,
							 cmyth_proginfo_t prog)
{
	return -ENOSYS;
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

	ret = malloc(len + 1);
	if (!ret) {
		return NULL;
	}
	cmyth_timestamp_to_string(start_ts, prog->proginfo_start_ts);
	cmyth_timestamp_to_string(end_ts, prog->proginfo_end_ts);
	cmyth_timestamp_to_string(rec_start_ts, prog->proginfo_rec_start_ts);
	cmyth_timestamp_to_string(rec_end_ts, prog->proginfo_rec_end_ts);
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
			prog->proginfo_Start,
			prog->proginfo_Length,
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

	ret = malloc(len + 1);
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
 * Retrieves the 'proginfo_chansign' field of a program info structure.
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
 * Retrieves the 'proginfo_channame' field of a program info structure.
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
 * Retrieves the 'proginfo_pathname' field of a program info structure.
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
 * The returned timestamp is returned held.  It should be released when no longer needed using cmyth_timestamp_release().
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
