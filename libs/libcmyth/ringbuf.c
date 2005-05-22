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
#include <errno.h>
#include <string.h>
#include <cmyth.h>
#include <cmyth_local.h>
#include <string.h>

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

	ret->conn_data = NULL;
	ret->ringbuf_url = NULL;
	ret->ringbuf_size = 0;
	ret->ringbuf_fill = 0;
	ret->file_pos = 0;
	ret->file_id = 0;
	ret->ringbuf_hostname = NULL;
	ret->ringbuf_port = 0;
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

int
cmyth_ringbuf_setup(cmyth_conn_t control, cmyth_recorder_t rec)
{
	static const char service[]="rbuf://";
	char *host = NULL;
	char *port = NULL;
	char *path = NULL;
	char tmp;

	int err, count;
	int r;
	long ret = 0;
	long long size, fill;
	char msg[256];
	char url[1024];

	if (!control || !rec) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no recorder connection\n",
			  __FUNCTION__);
		return -EINVAL;
	}

	pthread_mutex_lock(&mutex);

	snprintf(msg, sizeof(msg),
		 "QUERY_RECORDER %u[]:[]SETUP_RING_BUFFER[]:[]0",
		 rec->rec_id);

	if ((err=cmyth_send_message(control, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		ret = err;
		goto out;
	}

	count = cmyth_rcv_length(control);

	r = cmyth_rcv_string(control, &err, url, sizeof(url)-1, count); 
	count -= r;

	if ((r=cmyth_rcv_long_long(control, &err, &size, count)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  __FUNCTION__, r);
		ret = err;
		goto out;
	}
	count -= r;

	if ((r=cmyth_rcv_long_long(control, &err, &fill, count)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  __FUNCTION__, r);
		ret = err;
		goto out;
	}

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s: url is: '%s'\n",
			  __FUNCTION__, url);
	path = url;
	if (strncmp(url, service, sizeof(service) - 1) == 0) {
		/*
		 * The URL starts with rbuf://.  The rest looks like
		 * <host>:<port>/<filename>.
		 */
		host = url + strlen(service);
		port = strchr(host, ':');
		if (!port) {
			/*
			 * This does not seem to be a proper URL, so just
			 * assume it is a filename, and get out.
			 */
			fprintf(stderr, "1 port %s, host = %s\n", port, host);
			goto out;
		}
		port = port + 1;
		path = strchr(port, '/');
		if (!path) {
			/*
			 * This does not seem to be a proper URL, so just
			 * assume it is a filename, and get out.
			 */
			fprintf(stderr, "no path\n");
			goto out;
		}
	}

        rec->rec_ring = cmyth_ringbuf_create();
        
	tmp = *(port - 1);
	*(port - 1) = '\0';
	rec->rec_ring->ringbuf_hostname = strdup(host);
	*(port - 1) = tmp;
	tmp = *(path);
	*(path) = '\0';
	rec->rec_ring->ringbuf_port = atoi(port);
	*(path) = tmp;
	rec->rec_ring->ringbuf_url = strdup(url);
	rec->rec_ring->ringbuf_size = size;
	rec->rec_ring->ringbuf_fill = fill;

	ret = 0;

 out:
	pthread_mutex_unlock(&mutex);

	return ret;
}

char *
cmyth_ringbuf_pathname(cmyth_recorder_t rec)
{
        return (char *) rec->rec_ring->ringbuf_url;
}

/*
 * cmyth_ringbuf_get_block(cmyth_ringbuf_t file, char *buf, unsigned long len)
 * Scope: PUBLIC
 * Description
 * Read incoming file data off the network into a buffer of length len.
 *
 * Return Value:
 * Sucess: number of bytes read into buf
 * Failure: -1
 */
int
cmyth_ringbuf_get_block(cmyth_recorder_t rec, char *buf, unsigned long len)
{
	struct timeval tv;
	fd_set fds;

	if (rec == NULL)
		return -EINVAL;

	tv.tv_sec = 10;
	tv.tv_usec = 0;
	FD_ZERO(&fds);
	FD_SET(rec->rec_ring->conn_data->conn_fd, &fds);
	if (select(rec->rec_ring->conn_data->conn_fd+1, NULL, &fds, NULL, &tv) == 0) {
		rec->rec_ring->conn_data->conn_hang = 1;
		return 0;
	} else {
		rec->rec_ring->conn_data->conn_hang = 0;
	}
	return read(rec->rec_ring->conn_data->conn_fd, buf, len);
}

int
cmyth_ringbuf_select(cmyth_recorder_t rec, struct timeval *timeout)
{
	fd_set fds;
	int fd, ret;
	if (rec == NULL)
		return -EINVAL;

	fd = rec->rec_ring->conn_data->conn_fd;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	ret = select(fd+1, &fds, NULL, NULL, timeout);

	if (ret == 0)
		rec->rec_ring->conn_data->conn_hang = 1;
	else
		rec->rec_ring->conn_data->conn_hang = 0;

	return ret;
}

/*
 * cmyth_ringbuf_request_block(cmyth_ringbuf_t control, cmyth_ringbuf_t file,
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
cmyth_ringbuf_request_block(cmyth_conn_t control, cmyth_recorder_t rec,
			 unsigned long len)
{
	int err, count;
	int r;
	long c, ret;
	char msg[256];

	if (!rec) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n",
			  __FUNCTION__);
		return -EINVAL;
	}

	pthread_mutex_lock(&mutex);

	snprintf(msg, sizeof(msg),
		 "QUERY_RECORDER %u[]:[]REQUEST_BLOCK_RINGBUF[]:[]%ld",
		 rec->rec_id, len);

	if ((err = cmyth_send_message(control, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		ret = err;
		goto out;
	}

	count = cmyth_rcv_length(control);
	if ((r=cmyth_rcv_long(control, &err, &c, count)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  __FUNCTION__, r);
		ret = err;
		goto out;
	}

	rec->rec_ring->file_pos += c;
	ret = c;

 out:
	pthread_mutex_unlock(&mutex);

	return ret;
}

/*
 * cmyth_ringbuf_seek(cmyth_ringbuf_t control, cmyth_ringbuf_t file, long long offset,
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
cmyth_ringbuf_seek(cmyth_conn_t control, cmyth_recorder_t rec,
		   long long offset, int whence)
{
	char msg[128];
	int err;
	int count;
	long long c;
	long r;
	long long ret;
	cmyth_ringbuf_t ring;

	if ((control == NULL) || (rec == NULL))
		return -EINVAL;

	ring = rec->rec_ring;

	if ((offset == 0) && (whence == SEEK_CUR))
		return ring->file_pos;

	pthread_mutex_lock(&mutex);

	snprintf(msg, sizeof(msg),
		 "QUERY_RECORDER %ld[]:[]SEEK_RINGBUF[]:[]%ld[]:[]%ld[]:[]%d[]:[]%ld[]:[]%ld",
		 ring->file_id,
		 (long)(offset >> 32),
		 (long)(offset & 0xffffffff),
		 whence,
		 (long)(ring->file_pos >> 32),
		 (long)(ring->file_pos & 0xffffffff));

	if ((err = cmyth_send_message(control, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_send_message() failed (%d)\n",
			  __FUNCTION__, err);
		ret = err;
		goto out;
	}

	count = cmyth_rcv_length(control);
	if ((r=cmyth_rcv_long_long(control, &err, &c, count)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  __FUNCTION__, r);
		ret = err;
		goto out;
	}

	switch (whence) {
	case SEEK_SET:
		ring->file_pos = offset;
		break;
	case SEEK_CUR:
		ring->file_pos += offset;
		break;
	case SEEK_END:
		ring->file_pos = ring->file_length - offset;
		break;
	}

	ret = ring->file_pos;

 out:
	pthread_mutex_unlock(&mutex);
	
	return ret;
}
