/*
 *  Copyright (C) 2005, Jon Gettler
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
#include <string.h>
#include <errno.h>
#include <cmyth.h>
#include <cmyth_local.h>

cmyth_event_t
cmyth_event_get(cmyth_conn_t conn)
{
	int count, err, consumed;
	char tmp[1024];
	cmyth_event_t event;

	if (conn == NULL)
		goto fail;

	if ((count=cmyth_rcv_length(conn)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_length() failed (%d)\n",
			  __FUNCTION__, count);
		goto fail;
	}

	consumed = cmyth_rcv_string(conn, &err, tmp, sizeof(tmp) - 1, count);
	count -= consumed;
	if (strcmp(tmp, "BACKEND_MESSAGE") != 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_string() failed (%d)\n",
			  __FUNCTION__, count);
		goto fail;
	}

	consumed = cmyth_rcv_string(conn, &err, tmp, sizeof(tmp) - 1, count);
	count -= consumed;
	if (strcmp(tmp, "RECORDING_LIST_CHANGE") == 0) {
		event = CMYTH_EVENT_RECORDING_LIST_CHANGE;
	} else if (strcmp(tmp, "SCHEDULE_CHANGE") == 0) {
		event = CMYTH_EVENT_SCHEDULE_CHANGE;
	} else {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_string() failed (%d)\n",
			  __FUNCTION__, count);
		goto fail;
	}

	consumed = cmyth_rcv_string(conn, &err, tmp, sizeof(tmp) - 1, count);
	count -= consumed;
	if (strcmp(tmp, "empty") != 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
			  "%s: cmyth_rcv_string() failed (%d)\n",
			  __FUNCTION__, count);
		goto fail;
	}

	return event;

 fail:
	return CMYTH_EVENT_NONE;
}
