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
 * Sucess: 0
 *
 * Failure: an int containing -errno
 */
int
cmyth_file_release(cmyth_conn_t control, cmyth_file_t file)
{
	int err, count;
	int r;
	long c;
	char msg[256];

	if (!file) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n",
			  __FUNCTION__);
		return -EINVAL;
	}

	/*
	 * do not close the file connection if it is not the last reference
	 */
	if (cmyth_atomic_dec_and_test(&file->refcount) == 0)
		return 0;

	snprintf(msg, sizeof(msg),
		 "QUERY_FILETRANSFER %ld[]:[]DONE", file->file_id);

	if ((err = cmyth_send_message(control, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		goto fail;
	}

	count = cmyth_rcv_length(control);
	if ((r=cmyth_rcv_long(control, &err, &c, count)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  __FUNCTION__, r);
		goto fail;
	}

	/*
	 * Last reference, free it.
	 */
	cmyth_file_destroy(file);

	return 0;

 fail:
	cmyth_atomic_inc(&file->refcount);

	return err;
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

/*
 * cmyth_file_get_block(cmyth_file_t file, char *buf, unsigned long len)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Read incoming file data off the network into a buffer of length len.
 *
 * Return Value:
 *
 * Sucess: number of bytes read into buf
 *
 * Failure: -1
 */
int
cmyth_file_get_block(cmyth_file_t file, char *buf, unsigned long len)
{
	if (file == NULL)
		return -EINVAL;

	return read(file->file_data->conn_fd, buf, len);
}

int
cmyth_file_select(cmyth_file_t file, struct timeval *timeout)
{
	fd_set fds;
	int fd;

	if (file == NULL)
		return -EINVAL;

	fd = file->file_data->conn_fd;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	return select(fd+1, &fds, NULL, NULL, timeout);
}

/*
 * cmyth_file_request_block(cmyth_file_t control, cmyth_file_t file,
 *                          unsigned long len)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Request a file data block of a certain size, and return when the
 * block has been transfered.
 *
 * Return Value:
 *
 * Sucess: number of bytes transfered
 *
 * Failure: an int containing -errno
 */
int
cmyth_file_request_block(cmyth_conn_t control, cmyth_file_t file,
			 unsigned long len)
{
	int err, count;
	int r;
	long c;
	char msg[256];

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

	count = cmyth_rcv_length(control);
	if ((r=cmyth_rcv_long(control, &err, &c, count)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  __FUNCTION__, r);
		return err;
	}

	file->file_pos += c;

	return c;
}

/*
 * cmyth_file_seek(cmyth_file_t control, cmyth_file_t file, long long offset,
 *                 int whence)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Seek to a new position in the file based on the value of whence:
 *	SEEK_SET
 *		The offset is set to offset bytes.
 *	SEEK_CUR
 *		The offset is set to the current position plus offset bytes.
 *	SEEK_END
 *		The offset is set to the size of the file minus offset bytes.
 *
 * Return Value:
 *
 * Sucess: 0
 *
 * Failure: an int containing -errno
 */
long long
cmyth_file_seek(cmyth_conn_t control, cmyth_file_t file, long long offset,
		int whence)
{
	char msg[128];
	int err;
	int count;
	long long c;
	long r;
	long hi, lo;

	if ((control == NULL) || (file == NULL))
		return -EINVAL;

	if ((offset == 0) && (whence == SEEK_CUR))
		return file->file_pos;

	snprintf(msg, sizeof(msg),
		 "QUERY_FILETRANSFER %ld[]:[]SEEK[]:[]%ld[]:[]%ld[]:[]%d[]:[]%ld[]:[]%ld",
		 file->file_id,
		 (long)(offset >> 32),
		 (long)(offset & 0xffffffff),
		 whence,
		 (long)(file->file_pos >> 32),
		 (long)(file->file_pos & 0xffffffff));

	if ((err = cmyth_send_message(control, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		return err;
	}

	count = cmyth_rcv_length(control);
	if ((r=cmyth_rcv_long_long(control, &err, &c, count)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  __FUNCTION__, r);
		return err;
	}

	switch (whence) {
	case SEEK_SET:
		file->file_pos = offset;
		break;
	case SEEK_CUR:
		file->file_pos += offset;
		break;
	case SEEK_END:
		file->file_pos = file->file_length - offset;
		break;
	}
	
	return file->file_pos;
}
