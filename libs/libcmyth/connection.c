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
 * connection.c - functions to handle creating connections to a MythTV backend
 *                and interacting with those connections.  
 */
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <cmyth.h>
#include <cmyth_local.h>

/*
 * cmyth_conn_create(void)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Allocate and initialize a cmyth_conn_t structure.  This should only
 * be called by cmyth_connect(), which establishes a connection.
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_conn_t (this type is a pointer)
 *
 * Failure: A NULL cmyth_conn_t
 */
static cmyth_conn_t
cmyth_conn_create(void)
{
	cmyth_conn_t ret = malloc(sizeof(*ret));

	if (!ret) {
		return NULL;
	}
	ret->conn_fd = -1;
	ret->conn_buf = NULL;
	ret->conn_len = 0;
	ret->conn_buflen = 0;
	ret->conn_pos = 0;
	cmyth_atomic_set(&ret->refcount, 1);
	return ret;
}

/*
 * cmyth_conn_destroy(cmyth_conn_t conn)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Tear down and release storage associated with a connection.  This
 * should only be called by cmyth_conn_release().  All others should
 * call cmyth_conn_release() to release a connection.
 *
 * Return Value:
 *
 * None.
 */
static void
cmyth_conn_destroy(cmyth_conn_t conn)
{
	if (!conn) {
		return;
	}
	if (conn->conn_buf) {
		free(conn->conn_buf);
	}
	if (conn->conn_fd >= 0) {
		shutdown(conn->conn_fd, 2);
		close(conn->conn_fd);
	}
	free(conn);
}

/*
 * cmyth_conn_hold(cmyth_conn_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Take a new reference to a connection structure.  Connection structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the connection will be
 * destroyed.  This function is how one creates a new holder of a
 * connection.  This function always returns the pointer passed to it.
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
cmyth_conn_t
cmyth_conn_hold(cmyth_conn_t p)
{
	if (p) {
		cmyth_atomic_inc(&p->refcount);
	}
	return p;
}

/*
 * cmyth_conn_release(cmyth_conn_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Release a reference to a connection structure.  Connection structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the connection will be
 * destroyed.  This function is how one drops a reference to a
 * connection.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_conn_release(cmyth_conn_t p)
{
	if (p) {
		if (cmyth_atomic_dec_and_test(&p->refcount)) {
			/*
			 * Last reference, free it.
			 */
			cmyth_conn_destroy(p);
		}
	}
}

/*
 * cmyth_connect(char *server, unsigned short port, unsigned buflen)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Create a connection to the port specified by 'port' on the
 * server named 'server'.  This creates a data structure called a
 * cmyth_conn_t which contains the file descriptor for a socket, a
 * buffer for reading from the socket, and information used to manage
 * offsets in that buffer.  The buffer length is specified in 'buflen'.
 *
 * The returned connection has a single reference.  The connection
 * will be shut down and closed when the last reference is released
 * using cmyth_conn_release().
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_conn_t (this type is a pointer)
 *
 * Failure: A NULL cmyth_conn_t
 */
static char my_hostname[128];

cmyth_conn_t
cmyth_connect(char *server, unsigned short port, unsigned buflen)
{
	cmyth_conn_t ret = NULL;
	struct hostent *host;
	struct sockaddr_in addr;
	unsigned char *buf = NULL;
	int fd;

	/*
	 * First try to establish the connection with the server.
	 * If this fails, we are going no further.
	 */
	host = gethostbyname(server);
	if (!host) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cannot resolve hostname '%s'\n",
				  __FUNCTION__, server);
		return NULL;
	}
	if (host->h_addrtype != AF_INET) {
		/*
		 * For now, this should only be IPv4, perhaps later I can
		 * branch out...
		 */
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no AF_INET address for '%s'\n",
				  __FUNCTION__, server);
		return NULL;
	}
	addr.sin_family = host->h_addrtype;
	addr.sin_port  = htons(port);
	memcpy(&addr.sin_addr, host->h_addr_list[0], host->h_length);

	fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cannot create socket (%d)\n",
				  __FUNCTION__, errno);
		return NULL;
	}

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s: connecting to %d.%d.%d.%d\n",
			  __FUNCTION__,
			  (ntohl(addr.sin_addr.s_addr) & 0xFF000000) >> 24,
			  (ntohl(addr.sin_addr.s_addr) & 0x00FF0000) >> 16,
			  (ntohl(addr.sin_addr.s_addr) & 0x0000FF00) >>  8,
			  (ntohl(addr.sin_addr.s_addr) & 0x000000FF));
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
				  "%s: connect failed on port %d to '%s' (%d)\n",
				  __FUNCTION__, port, server, errno);
		close(fd);
		return NULL;
	}

	if ((my_hostname[0] == '\0') &&
		(gethostname(my_hostname, sizeof(my_hostname)) < 0)) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: gethostname failed (%d)\n",
				  __FUNCTION__, errno);
		goto shut;
	}

	/*
	 * Okay, we are connected. Now is a good time to allocate some
	 * resources.
	 */
	buf = malloc(buflen * sizeof(unsigned char));
	if (!buf) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s:- malloc(%d) failed allocating buf\n",
				  __FUNCTION__, buflen * sizeof(unsigned char *));
		goto shut;
	}
	ret = cmyth_conn_create();
	if (!ret) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_conn_create() failed\n",
				  __FUNCTION__);
		goto shut;
	}
	ret->conn_fd = fd;
	ret->conn_buflen = buflen;
	ret->conn_buf = buf;
	ret->conn_len = 0;
	ret->conn_pos = 0;
	ret->conn_version = 8;
	return ret;

 shut:
	if (buf) {
		free(buf);
	}
	if (ret) {
		free(ret);
	}
	shutdown(fd, 2);
	close(fd);
	return NULL;
}

/*
 * cmyth_conn_connect_ctrl(char *server, unsigned short port, unsigned buflen)
 *
 * Scope: PUBLIC
 *
 * Description:
 *
 * Create a connection for use as a control connection within the
 * MythTV protocol.  Return a pointer to the newly created connection.
 * The connection is returned held, and may be released using
 * cmyth_conn_release().
 *
 * Return Value:
 *
 * Success: Non-NULL cmyth_conn_t (this is a pointer type)
 *
 * Failure: NULL cmyth_conn_t
 */
cmyth_conn_t
cmyth_conn_connect_ctrl(char *server, unsigned short port, unsigned buflen)
{
	cmyth_conn_t conn;
	char announcement[256];
	unsigned long tmp_ver;

	conn = cmyth_connect(server, port, buflen);
	if (!conn) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_connect(%s, %d, %d) failed\n",
				  __FUNCTION__, server, port, buflen);
		return NULL;
	}

	/*
	 * Find out what the Myth Protocol Version is for this connection.
	 * Loop around until we get agreement from the server.
	 */
	tmp_ver = conn->conn_version;
	do {
		conn->conn_version = tmp_ver;
		sprintf(announcement, "MYTH_PROTO_VERSION %ld", conn->conn_version);
		if (cmyth_send_message(conn, announcement) < 0) {
			cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_send_message('%s') failed\n",
					  __FUNCTION__, announcement);
			goto shut;
		}
		if (cmyth_rcv_version(conn, &tmp_ver) < 0) {
			cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_version() failed\n",
					  __FUNCTION__);
			goto shut;
		}
	} while (conn->conn_version != tmp_ver);
	printf("%s: agreed on Version %ld protocol\n",
		   __FUNCTION__, conn->conn_version);
	cmyth_dbg(CMYTH_DBG_ERROR, "%s: agreed on Version %ld protocol\n",
			  __FUNCTION__, conn->conn_version);

	sprintf(announcement, "ANN Playback %s 0", my_hostname);
	if (cmyth_send_message(conn, announcement) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_send_message('%s') failed\n",
				  __FUNCTION__, announcement);
		goto shut;
	}
	if (cmyth_rcv_okay(conn, "OK") < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_okay() failed\n",
				  __FUNCTION__);
		goto shut;
	}
	return conn;

 shut:
	cmyth_conn_release(conn);
	return NULL;
}

/*
 * cmyth_conn_connect_file(char *server, unsigned short port, unsigned buflen
 *                         cmyth_proginfo_t prog)
 *
 * Scope: PUBLIC
 *
 * Description:
 *
 * Create a file structure containing a data connection for use
 * transfering a file within the MythTV protocol.  Return a pointer to
 * the newly created file structure.  The connection in the file
 * structure is returned held as is the file structure itself.  The
 * connection will be released when the file structure is released.
 * The file structure can be released using cmyth_file_release().
 *
 * Return Value:
 *
 * Success: Non-NULL cmyth_file_t (this is a pointer type)
 *
 * Failure: NULL cmyth_file_t
 */
cmyth_file_t
cmyth_conn_connect_file(cmyth_proginfo_t prog, unsigned buflen)
{
	cmyth_conn_t conn = NULL;
	char *announcement = NULL;
	char reply[16];
	int err = 0;
	int count = 0;
	int r;
	int ann_size = sizeof("ANN FileTransfer []:[]");
	cmyth_file_t ret = NULL;

	if (!prog) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: prog is NULL\n", __FUNCTION__);
		goto shut;
	}
	if (!prog->proginfo_host) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: prog host is NULL\n", __FUNCTION__);
		goto shut;
	}
	if (!prog->proginfo_pathname) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: prog has no pathname in it\n",
				  __FUNCTION__);
		goto shut;
	}
	ret = cmyth_file_create();
	if (!ret) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_file_create() failed\n",
				  __FUNCTION__);
		goto shut;
	}
	conn = cmyth_connect(prog->proginfo_host, prog->proginfo_port, buflen);
	if (!conn) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_connect(%s, %d, %d) failed\n",
				  __FUNCTION__, prog->proginfo_host, prog->proginfo_port,
				  buflen);
		goto shut;
	}
	ann_size += strlen(prog->proginfo_pathname) + strlen(my_hostname);
	announcement = malloc(ann_size);
	if (!announcement) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: malloc(%d) failed for announcement\n",
				  __FUNCTION__, ann_size);
		goto shut;
	}
	sprintf(announcement, "ANN FileTransfer %s[]:[]%s",
			my_hostname, prog->proginfo_pathname);
	printf("PATH: '%s'\n", prog->proginfo_pathname);
	if (cmyth_send_message(conn, announcement) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_send_message('%s') failed\n",
				  __FUNCTION__, announcement);
		goto shut;
	}
	ret->file_data = conn;
	count = cmyth_rcv_length(conn);
	if (count < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_length() failed (%d)\n",
				  __FUNCTION__, count);
		goto shut;
	}
	reply[sizeof(reply) - 1] = '\0';
	r = cmyth_rcv_string(conn, &err, reply, sizeof(reply) - 1, count); 
	if (err != 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_string() failed (%d)\n",
				  __FUNCTION__, err);
		goto shut;
	}
	if (strcmp(reply, "OK") != 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: reply ('%s') is not 'OK'\n",
				  __FUNCTION__, reply);
		goto shut;
	}
	count -= r;
	r = cmyth_rcv_long(conn, &err, &ret->file_id, count);
	if (err) {
		cmyth_dbg(CMYTH_DBG_ERROR,
				  "%s: (id) cmyth_rcv_long() failed (%d)\n",
				  __FUNCTION__, err);
		goto shut;
	}
	count -= r;
	r = cmyth_rcv_long_long(conn, &err, &ret->file_start, count);
	if (err) {
		cmyth_dbg(CMYTH_DBG_ERROR,
				  "%s: (start) cmyth_rcv_longlong() failed (%d)\n",
				  __FUNCTION__, err);
		goto shut;
	}
	count -= r;
	r = cmyth_rcv_long_long(conn, &err, &ret->file_length, count);
	if (err) {
		cmyth_dbg(CMYTH_DBG_ERROR,
				  "%s: (length) cmyth_rcv_longlong() failed (%d)\n",
				  __FUNCTION__, err);
		goto shut;
	}
	count -= r;
	free(announcement);
	return ret;

 shut:
	if (announcement) {
		free(announcement);
	}
	if (ret) {
		cmyth_file_release(ret);
	}
	if (conn) {
		cmyth_conn_release(conn);
	}
	return NULL;
}

/*
 * cmyth_conn_connect_ring(char *server, unsigned short port, unsigned buflen
 *                         cmyth_recorder_t rec)
 *
 * Scope: PUBLIC
 *
 * Description:
 *
 * Create a new ring buffer connection for use transferring live-tv
 * using the MythTV protocol.  Return a pointer to the newly created
 * ring buffer connection.  The ring buffer connection is returned
 * held, and may be released using cmyth_conn_release().
 *
 * Return Value:
 *
 * Success: Non-NULL cmyth_conn_t (this is a pointer type)
 *
 * Failure: NULL cmyth_conn_t
 */
cmyth_conn_t
cmyth_conn_connect_ring(char *server, unsigned short port, unsigned buflen,
						cmyth_recorder_t rec)
{
	cmyth_conn_t conn;
	char *announcement;
	int ann_size = sizeof("ANN RingBuffer  ");

	if (!rec) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: rec is NULL\n", __FUNCTION__);
		return NULL;
	}

	conn = cmyth_connect(server, port, buflen);
	if (!conn) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_connect(%s, %d, %d) failed\n",
				  __FUNCTION__, server, port, buflen);
		return NULL;
	}

	ann_size += CMYTH_LONG_LEN + strlen(my_hostname);
	announcement = malloc(ann_size);
	if (!announcement) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: malloc(%d) failed for announcement\n",
				  __FUNCTION__, ann_size);
		goto shut;
	}
	sprintf(announcement,
			"ANN RingBuffer %s %d", my_hostname, rec->rec_id);
	if (cmyth_send_message(conn, announcement) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_send_message('%s') failed\n",
				  __FUNCTION__, announcement);
		free(announcement);
		goto shut;
	}
	free(announcement);
	if (cmyth_rcv_okay(conn, "OK") < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_okay() failed\n",
				  __FUNCTION__);
		goto shut;
	}
	return conn;

 shut:
	cmyth_conn_release(conn);
	return NULL;
}

/*
 * cmyth_conn_check_block(cmyth_conn_t conn, unsigned long size)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Check whether a block has finished transfering from a backend
 * server. This non-blocking check looks for a response from the
 * server indicating that a block has been entirely sent to on a data
 * socket.
 *
 * Return Value:
 *
 * Success: 0 for not complete, 1 for complete
 *
 * Failure: -(errno)
 */
int
cmyth_conn_check_block(cmyth_conn_t conn, unsigned long size)
{
	fd_set check;
	struct timeval timeout = { .tv_usec = 0, .tv_sec = 0 };
	int length;
	int err = 0;
	unsigned long sent;

	if (!conn) {
		return -EINVAL;
	}
	FD_ZERO(&check);
	FD_SET(conn->conn_fd, &check);
	if (select(conn->conn_fd + 1, &check, NULL, NULL, &timeout) < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s: select failed (%d)\n",
				  __FUNCTION__, errno);
		return -(errno);
	}
	if (FD_ISSET(conn->conn_fd, &check)) {
		/*
		 * We have a bite, reel it in.
		 */
		length = cmyth_rcv_length(conn);
		if (length < 0) {
			return length;
		}
		cmyth_rcv_ulong(conn, &err, &sent, length);
		if (err) {
			return -err;
		}
		if (sent == size) {
			/*
			 * This block has been sent, return TRUE.
			 */
			cmyth_dbg(CMYTH_DBG_DEBUG, "%s: block finished (%d bytes)\n",
					  __FUNCTION__, sent);
			return 1;
		} else {
			cmyth_dbg(CMYTH_DBG_ERROR, "%s: block finished short (%d bytes)\n",
					  __FUNCTION__, sent);
			return -ECANCELED;
		}
	}
	return 0;
}

/*
 * cmyth_conn_get_recorder_from_num(cmyth_conn_t control,
 *                                  cmyth_recorder_num_t num,
 *                                  cmyth_recorder_t rec)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Obtain a recorder from a connection by its recorder number.  The
 * recorder structure created by this describes how to set up a data
 * connection and play media streamed from a particular back-end recorder.
 *
 * This fills out the recorder structure specified by 'rec'.
 *
 * Return Value:
 *
 * Success: 0 for not complete, 1 for complete
 *
 * Failure: -(errno)
 */
cmyth_recorder_t
cmyth_conn_get_recorder_from_num(cmyth_conn_t conn,
								 cmyth_rec_num_t num)
{
	return NULL;
}

/*
 * cmyth_conn_get_free_recorder(cmyth_conn_t control, cmyth_recorder_t rec)
 *                             
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Obtain the next available free recorder the connection specified by
 * 'control'.  This fills out the recorder structure specified by 'rec'.
 *
 * Return Value:
 *
 * Success: 0 for not complete, 1 for complete
 *
 * Failure: -(errno)
 */
cmyth_recorder_t
cmyth_conn_get_free_recorder(cmyth_conn_t conn, cmyth_recorder_t rec)
{
	return NULL;
}
