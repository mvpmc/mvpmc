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
 * timestamp.c - functions to manage MythTV timestamps.  Primarily,
 *               these allocate timestamps and convert between string
 *               and cmyth_timestamp_t and between long long and
 *               cmyth_timestamp_t.
 */
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <cmyth.h>
#include <cmyth_local.h>
#include <time.h>

/*
 * cmyth_timestamp_create(void)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Create a timestamp structure and return a pointer to the structure.
 *
 * Return Value:
 *
 * Success: A non-NULL cmyth_timestamp_t (this type is a pointer)
 *
 * Failure: A NULL cmyth_timestamp_t
 */
cmyth_timestamp_t
cmyth_timestamp_create(void)
{
	cmyth_timestamp_t ret = malloc(sizeof(*ret));

	if (!ret) {
		return(NULL);
	}
	ret->timestamp_year = 0;
	ret->timestamp_month = 0;
	ret->timestamp_day = 0;
	ret->timestamp_hour = 0;
	ret->timestamp_minute = 0;
	ret->timestamp_second = 0;
	ret->timestamp_isdst = 0;
	cmyth_atomic_set(&ret->refcount, 1);
	return ret;
}

/*
 * cmyth_timestamp_destroy(void)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * Destroy and free a timestamp structure.  This should only be called
 * by cmyth_timestamp_release().  All others should use
 * cmyth_timestamp_release() to release references to time stamps.
 *
 * Return Value:
 *
 * None.
 */
static void
cmyth_timestamp_destroy(cmyth_timestamp_t ts)
{
	if (!ts) {
		return;
	}
	free(ts);
}

/*
 * cmyth_timestamp_hold(cmyth_timestamp_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Take a new reference to a timestamp structure.  Timestamp structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the timestamp will be
 * destroyed.  This function is how one creates a new holder of a
 * timestamp.  This function always returns the pointer passed to it.
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
cmyth_timestamp_t
cmyth_timestamp_hold(cmyth_timestamp_t p)
{
	if (p) {
		cmyth_atomic_inc(&p->refcount);
	}
	return p;
}

/*
 * cmyth_timestamp_release(cmyth_timestamp_t p)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Release a reference to a timestamp structure.  Timestamp structures
 * are reference counted to facilitate caching of pointers to them.
 * This allows a holder of a pointer to release their hold and trust
 * that once the last reference is released the timestamp will be
 * destroyed.  This function is how one drops a reference to a
 * timestamp.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_timestamp_release(cmyth_timestamp_t p)
{
	if (p) {
		if (cmyth_atomic_dec_and_test(&p->refcount)) {
			/*
			 * Last reference, free it.
			 */
			cmyth_timestamp_destroy(p);
		}
	}
}

/*
 * cmyth_timestamp_from_string(cmyth_timestamp_t ts, char *str)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Fill out the timestamp structure pointed to by 'ts' using the
 * string 'str'.  The string must be a timestamp of the forn:
 *
 *    yyyy-mm-ddThh:mm:ss
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_timestamp_from_string(cmyth_timestamp_t ts, char *str)
{
	int i;
	int datetime = 1;
	char *yyyy = &str[0];
	char *MM = &str[5];
	char *dd = &str[8];
	char *hh = &str[11];
	char *mm = &str[14];
	char *ss = &str[17];
	
	if (!str) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL string\n", __FUNCTION__);
		return -EINVAL;
	}
	if (!ts) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL timestamp\n",
			  __FUNCTION__);
		return -EINVAL;
	}
	if (strlen(str) != CMYTH_TIMESTAMP_LEN) {
		datetime = 0;
		if (strlen(str) != CMYTH_DATESTAMP_LEN) {
			cmyth_dbg(CMYTH_DBG_ERROR,
				  "%s: string is not a timestamp '%s'\n",
				  __FUNCTION__, str);
			return -EINVAL;
		}
	}

	if ((datetime == 1) &&
	    ((str[4] != '-') || (str[7] != '-') || (str[10] != 'T') ||
	     (str[13] != ':') || (str[16] != ':'))) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: string is badly formed '%s'\n",
			  __FUNCTION__, str);
		return -EINVAL;
	}
	if ((datetime == 0) &&
	    ((str[4] != '-') || (str[7] != '-'))) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: string is badly formed '%s'\n",
			  __FUNCTION__, str);
		return -EINVAL;
	}

	/*
	 * XXX: Do not zero out the structure, since that will change the
	 *      reference count.  This should be fixed by creating new
	 *      timestamps each time.
	 *
	 *      This is actually going to be fixed soon by using the newly
	 *      created reference counted memory allocator and getting the
	 *      reference count out of the timestamp structure.  This will
	 *      allow the memset() to go back in.
	 */
#if 0
	memset(ts, 0, sizeof(*ts));
#endif

	str[4] = '\0';
	str[7] = '\0';
	if (datetime) {
		str[10] = '\0';
		str[13] = '\0';
		str[16] = '\0';
	}
	for (i = 0;
	     i < (datetime ? CMYTH_TIMESTAMP_LEN : CMYTH_DATESTAMP_LEN);
	     ++i) {
		if (str[i] && !isdigit(str[i])) {
			cmyth_dbg(CMYTH_DBG_ERROR,
				  "%s: expected numeral at '%s'[%d]\n",
				  __FUNCTION__, str, i);
			return -EINVAL;
		}
	}
	ts->timestamp_year = atoi(yyyy);
	ts->timestamp_month = atoi(MM);
	if (ts->timestamp_month > 12) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: month value too big'%s'\n",
			  __FUNCTION__, str);
		return -EINVAL;
	}
	ts->timestamp_day = atoi(dd);
	if (ts->timestamp_day > 31) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: day value too big'%s'\n",
			  __FUNCTION__, str);
		return -EINVAL;
	}

	if (datetime == 0)
		return 0;

	ts->timestamp_hour = atoi(hh);
	if (ts->timestamp_hour > 23) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: hour value too big'%s'\n",
			  __FUNCTION__, str);
		return -EINVAL;
	}
	ts->timestamp_minute = atoi(mm);
	if (ts->timestamp_minute > 59) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: minute value too big'%s'\n",
			  __FUNCTION__, str);
		return -EINVAL;
	}
	ts->timestamp_second = atoi(ss);
	if (ts->timestamp_second > 59) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: second value too big'%s'\n",
			  __FUNCTION__, str);
		return -EINVAL;
	}
	return 0;
}

/*
 * cmyth_datetime_from_string(cmyth_timestamp_t ts, char *str)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Fill out the timestamp structure pointed to by 'ts' using the
 * string 'str'.  The string must be a timestamp of the forn:
 *
 *    yyyy-mm-ddThh:mm:ss
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_datetime_from_string(cmyth_timestamp_t ts, char *str)
{
	unsigned int isecs;
	struct tm *tm_datetime;
	time_t t_datetime;
	
	if (!str) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL string\n", __FUNCTION__);
		return -EINVAL;
	}
	if (!ts) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL timestamp\n",
			  __FUNCTION__);
		return -EINVAL;
	}

	memset(ts, 0, sizeof(*ts));

	isecs=atoi(str);
	t_datetime = isecs;
	tm_datetime = localtime(&t_datetime);
	ts->timestamp_year = tm_datetime->tm_year + 1900;
	ts->timestamp_month = tm_datetime->tm_mon + 1;
	ts->timestamp_day = tm_datetime->tm_mday;
	ts->timestamp_hour = tm_datetime->tm_hour;
	ts->timestamp_minute = tm_datetime->tm_min;
	ts->timestamp_second = tm_datetime->tm_sec;
	ts->timestamp_isdst = tm_datetime->tm_isdst;
	return 0;
}



/*
 * cmyth_timestamp_from_longlong(cmyth_timestamp_t ts, longlong l)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Fill out the timestamp structure pointed to by 'ts' using the
 * long long 'l'.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_timestamp_from_longlong(cmyth_timestamp_t ts, long long l)
{
	return -ENOSYS;
}

/*
 * cmyth_timestamp_to_longlong( cmyth_timestamp_t ts)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Create a long long value from the timestamp structure 'ts' and
 * return the result.
 * 
 *
 * Return Value:
 *
 * Success: long long time value > 0 (seconds from January 1, 1970)
 *
 * Failure: (long long) -(ERRNO)
 */
long long
cmyth_timestamp_to_longlong(cmyth_timestamp_t ts)
{
	return (long long)-ENOSYS;
}

/*
 * cmyth_timestamp_to_string(char *str, cmyth_timestamp_t ts)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Create a string from the timestamp structure 'ts' and put it in the
 * user supplied buffer 'str'.  The size of 'str' must be
 * CMYTH_TIMESTAMP_LEN + 1 or this will overwrite beyond 'str'.
 * 
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_timestamp_to_string(char *str, cmyth_timestamp_t ts)
{
	if (!str) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL output string provided\n",
			  __FUNCTION__);
		return -EINVAL;
	}
	if (!ts) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL timestamp provided\n",
			  __FUNCTION__);
		return -EINVAL;
	}
	sprintf(str,
		"%4.4ld-%2.2ld-%2.2ldT%2.2ld:%2.2ld:%2.2ld",
		ts->timestamp_year,
		ts->timestamp_month,
		ts->timestamp_day,
		ts->timestamp_hour,
		ts->timestamp_minute,
		ts->timestamp_second);
	return 0;
}


/*
 * cmyth_datetime_to_string(char *str, cmyth_timestamp_t ts)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Create a string from the timestamp structure 'ts' and put it in the
 * user supplied buffer 'str'.  The size of 'str' must be
 * CMYTH_DATETIME_LEN + 1 or this will overwrite beyond 'str'.
 * 
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(ERRNO)
 */
int
cmyth_datetime_to_string(char *str, cmyth_timestamp_t ts)
{
	struct tm tm_datetime;
	time_t t_datetime;

	if (!str) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL output string provided\n",
			  __FUNCTION__);
		return -EINVAL;
	}
	if (!ts) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL timestamp provided\n",
			  __FUNCTION__);
		return -EINVAL;
	}

	memset(&tm_datetime, 0, sizeof(tm_datetime));
	tm_datetime.tm_year = ts->timestamp_year - 1900;
	tm_datetime.tm_mon = ts->timestamp_month - 1;
	tm_datetime.tm_mday = ts->timestamp_day;
	tm_datetime.tm_hour = ts->timestamp_hour;
	tm_datetime.tm_min = ts->timestamp_minute;
	tm_datetime.tm_sec = ts->timestamp_second;
	tm_datetime.tm_isdst = ts->timestamp_isdst;
	t_datetime = mktime(&tm_datetime);
	sprintf(str,
		"%4.4ld-%2.2ld-%2.2ldT%2.2ld:%2.2ld:%2.2ld",
		ts->timestamp_year,
		ts->timestamp_month,
		ts->timestamp_day,
		ts->timestamp_hour,
		ts->timestamp_minute,
		ts->timestamp_second);
	cmyth_dbg(CMYTH_DBG_ERROR, "original timestamp string: %s \n",str);
	sprintf(str,"%lu",(unsigned long) t_datetime);
	cmyth_dbg(CMYTH_DBG_ERROR, "time in seconds: %s \n",str);
	
	return 0;
}



/*
 * cmyth_timestamp_compare(cmyth_timestamp_t ts1, cmyth_timestamp_t ts2)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Compare ts1 to ts2 and indicate whether ts1 is less than, equal to
 * or greater than ts1.
 * 
 *
 * Return Value:
 *
 * -1: ts1 is less than (erlier than) ts2
 *  0: ts1 is the same as ts2
 *  1: ts1 is greater than (later than) ts2
 */
int
cmyth_timestamp_compare(cmyth_timestamp_t ts1, cmyth_timestamp_t ts2)
{
	/*
	 * If either timestamp is NULL it is 'less than' the non-NULL one
	 * (this is a stretch, but it shouldn't happen).  If they are both
	 * NULL, they are equal.
	 */
	if (!ts1) {
		if (!ts2) {
			return 0;
		}
		return -1;
	}
	if (!ts2) {
		return 1;
	}
	if (ts1->timestamp_year != ts2->timestamp_year) {
		return (ts1->timestamp_year > ts2->timestamp_year) ? 1 : -1;
	}
	if (ts1->timestamp_month != ts2->timestamp_month) {
		return (ts1->timestamp_month > ts2->timestamp_month) ? 1 : -1;
	}
	if (ts1->timestamp_day != ts2->timestamp_day) {
		return (ts1->timestamp_day > ts2->timestamp_day) ? 1 : -1;
	}
	if (ts1->timestamp_hour != ts2->timestamp_hour) {
		return (ts1->timestamp_hour > ts2->timestamp_hour) ? 1 : -1;
	}
	if (ts1->timestamp_minute != ts2->timestamp_minute) {
		return (ts1->timestamp_minute > ts2->timestamp_minute) 
			? 1
			: -1;
	}
	if (ts1->timestamp_second != ts2->timestamp_second) {
		return (ts1->timestamp_second > ts2->timestamp_second)
			? 1
			: -1;
	}
	return 0;
}

int
cmyth_timestamp_diff(cmyth_timestamp_t ts1, cmyth_timestamp_t ts2)
{
	struct tm tm_datetime;
	time_t start, end;

	memset(&tm_datetime, 0, sizeof(tm_datetime));
	tm_datetime.tm_year = ts1->timestamp_year - 1900;
	tm_datetime.tm_mon = ts1->timestamp_month - 1;
	tm_datetime.tm_mday = ts1->timestamp_day;
	tm_datetime.tm_hour = ts1->timestamp_hour;
	tm_datetime.tm_min = ts1->timestamp_minute;
	tm_datetime.tm_sec = ts1->timestamp_second;
	tm_datetime.tm_isdst = ts1->timestamp_isdst;
	start = mktime(&tm_datetime);

	memset(&tm_datetime, 0, sizeof(tm_datetime));
	tm_datetime.tm_year = ts2->timestamp_year - 1900;
	tm_datetime.tm_mon = ts2->timestamp_month - 1;
	tm_datetime.tm_mday = ts2->timestamp_day;
	tm_datetime.tm_hour = ts2->timestamp_hour;
	tm_datetime.tm_min = ts2->timestamp_minute;
	tm_datetime.tm_sec = ts2->timestamp_second;
	tm_datetime.tm_isdst = ts2->timestamp_isdst;
	end = mktime(&tm_datetime);

	return (int)(end - start);
}
