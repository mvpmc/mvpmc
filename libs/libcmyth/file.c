#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <cmyth.h>
#include <cmyth_local.h>

/*
 * cmyth_file_create(void)
 * 
 * Scope: PRIVATE (mapped to __cmyth_file_create)
 *
 * Description
 *
 * Allocate and initialize a cmyth_file_t structure.  This should only
 * be called by cmyth_connect_file(), which establishes a file
 * transfer connection.
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_file_t (this type is a pointer)
 *
 * Failure: A NULL cmyth_file_t
 */
cmyth_file_t
cmyth_file_create(void)
{
	cmyth_file_t ret = malloc(sizeof(*ret));

	if (!ret) {
		return NULL;
	}
	ret->file_data = NULL;
	ret->file_id = -1;
	ret->file_start = 0;
	ret->file_length = 0;
	ret->file_pos = 0;
	cmyth_atomic_set(&ret->refcount, 1);
	return ret;
}

/*
 * cmyth_file_destroy(cmyth_file_t file)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Tear down and release storage associated with a file connection.
 * This should only be called by cmyth_file_release().  All others
 * should call cmyth_file_release() to release a file connection.
 *
 * Return Value:
 *
 * None.
 */
static void
cmyth_file_destroy(cmyth_file_t file)
{
	if (!file) {
		return;
	}
	if (file->file_data) {
		cmyth_conn_release(file->file_data);
	}
	free(file);
}

/*
 * cmyth_file_hold(cmyth_file_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Take a new reference to a file connection structure.  File
 * connection structures are reference counted to facilitate caching
 * of pointers to them.  This allows a holder of a pointer to release
 * their hold and trust that once the last reference is released the
 * file connection will be destroyed.  This function is how one
 * creates a new holder of a file connection.  This function always
 * returns the pointer passed to it.  While it cannot fail, if it is
 * passed a NULL pointer, it will do nothing.
 *
 * Return Value:
 *
 * Success: The value of 'p'
 *
 * Failure: There is no real failure case, but a NULL 'p' will result in a
 *          NULL return.
 */
cmyth_file_t
cmyth_file_hold(cmyth_file_t p)
{
	if (p) {
		cmyth_atomic_inc(&p->refcount);
	}
	return p;
}

/*
 * cmyth_file_release(cmyth_file_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Release a reference to a file connection structure.  File
 * connection structures are reference counted to facilitate caching
 * of pointers to them.  This allows a holder of a pointer to release
 * their hold and trust that once the last reference is released the
 * file connection will be destroyed.  This function is how one drops
 * a reference to a file connection.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_file_release(cmyth_file_t p)
{
	if (p) {
		if (cmyth_atomic_dec_and_test(&p->refcount)) {
			/*
			 * Last reference, free it.
			 */
			cmyth_file_destroy(p);
		}
	}
}

/*
 * cmyth_file_data(cmyth_file_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Obtain a held reference to the data connection inside of a file
 * connection.  This cmyth_conn_t can be used to read data from the
 * MythTV backend server during a file transfer.
 *
 * Return Value:
 *
 * Sucess: A non-null cmyth_conn_t (this is a pointer type)
 *
 * Failure: NULL
 */
cmyth_conn_t
cmyth_file_data(cmyth_file_t file)
{
	if (!file) {
		return NULL;
	}
	if (!file->file_data) {
		return NULL;
	}
	cmyth_conn_hold(file->file_data);
	return file->file_data;
}

/*
 * cmyth_file_start(cmyth_file_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Obtain the start offset in a file for the beginning of the data.
 *
 * Return Value:
 *
 * Sucess: a long long value >= 0
 *
 * Failure: a long long containing -errno
 */
unsigned long long
cmyth_file_start(cmyth_file_t file)
{
	if (!file) {
		return -EINVAL;
	}
	return file->file_start;
}

/*
 * cmyth_file_length(cmyth_file_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Obtain the length of the data in a file.
 *
 * Return Value:
 *
 * Sucess: a long long value >= 0
 *
 * Failure: a long long containing -errno
 */
unsigned long long
cmyth_file_length(cmyth_file_t file)
{
	if (!file) {
		return -EINVAL;
	}
	return file->file_length;
}

int
cmyth_file_request_block(cmyth_conn_t control, cmyth_file_t file, char *buf, unsigned long len)
{
	int err, count;
	int r, c;
	char msg[256];
	int tot = 0;

	if (!file) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n",
			  __FUNCTION__);
		return -EINVAL;
	}

	snprintf(msg, sizeof(msg),
		 "QUERY_FILETRANSFER %ld[]:[]REQUEST_BLOCK[]:[]%ld",
		 file->file_id, len);

	if ((err = cmyth_send_message(control, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		return err;
	}

	while (tot < len) {
		count = read(file->file_data->conn_fd, buf+tot, len-tot);
		if (count > 0)
			tot += count;
	}

	count = cmyth_rcv_length(control);
	if ((r=cmyth_rcv_long(control, err, &c, count)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  __FUNCTION__, r);
	}
#if 0
	printf("%s(): c is %d\n", __FUNCTION__, c);
#endif

	file->file_pos += tot;

	return tot;
}

int
cmyth_file_seek(cmyth_conn_t control, cmyth_file_t file, int delta)
{
	char msg[128];
	int err;
	int count;
	long long c;
	long r;

	snprintf(msg, sizeof(msg),
		 "QUERY_FILETRANSFER %ld[]:[]SEEK[]:[]%d[]:[]0[]:[]%d[]:[]%lld[]:[]0",
		 file->file_id,
		 delta, SEEK_CUR, file->file_pos);

	printf("%s(): line %d\n", __FUNCTION__, __LINE__);
	if ((err = cmyth_send_message(control, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		return err;
	}

	printf("%s(): line %d\n", __FUNCTION__, __LINE__);
	count = cmyth_rcv_length(control);
	printf("%s(): line %d\n", __FUNCTION__, __LINE__);
	if ((r=cmyth_rcv_long_long(control, err, &c, count)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  __FUNCTION__, r);
	}
	printf("%s(): line %d\n", __FUNCTION__, __LINE__);

	printf("SEEK: count %d c %lld\n", count, c);

	if (c >= 0)
		file->file_pos += delta;

	return 0;
}
