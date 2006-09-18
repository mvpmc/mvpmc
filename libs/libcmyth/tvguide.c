/*
 *  Copyright (C) 2006, Sergio Slobodrian
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

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <mysql/mysql.h>
#include <cmyth.h>
#include <cmyth_local.h>
#include <mvp_widget.h>

#define ALLOC_FRAC 10
#define ALLOC_BLK 12

#define MERGE_CELLS 1

#if 0
int ssdebug = 0;
#define PRINTF(x...) if(ssdebug) printf(x) 
#define TRC(fmt, args...) printf(fmt, ## args)
#else
#define PRINTF(x...)
#define TRC(fmt, args...) 
#endif

static mvpw_array_cell_theme mvpw_record_theme = {
	.cell_fg = MVPW_YELLOW,
	.cell_bg = MVPW_MIDNIGHTBLUE,
	.hilite_fg = MVPW_MIDNIGHTBLUE,
	.hilite_bg = MVPW_YELLOW,
};

int
mvp_tvguide_sql_check(cmyth_database_t db)
{
	MYSQL *mysql;

  mysql=mysql_init(NULL);
	if(mysql == NULL ||
		 !(mysql_real_connect(mysql,db->db_host,db->db_user,
													db->db_pass,db->db_name,0,NULL,0))) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_connect() Failed: %s\n",
                           __FUNCTION__, mysql_error(mysql));
		fprintf(stderr, "mysql_connect() Failed: %s\n",mysql_error(mysql));

    if(mysql) mysql_close(mysql);
		return 0;
	}
  mysql_close(mysql);
	return 1;
}

/*
 *
 */
void
mvp_tvguide_move(int direction, mvp_widget_t * proglist, mvp_widget_t * descr)
{
	cmyth_tvguide_program_t prog;

	mvpw_move_array_selection(proglist, direction);
	prog = (cmyth_tvguide_program_t)
 	mvpw_get_array_cur_cell_data(proglist);
 	mvpw_set_text_str(descr, prog->description);
}

/*
 *
 */
void
mvp_tvguide_show(mvp_widget_t *proglist, mvp_widget_t *descr,
								 mvp_widget_t *clock)
{
	cmyth_tvguide_program_t prog;

	mvpw_show(descr);
	mvpw_show(clock);
	mvpw_reset_array_selection(proglist);
	mvpw_show(proglist);
	prog = (cmyth_tvguide_program_t)
 	mvpw_get_array_cur_cell_data(proglist);
 	mvpw_set_text_str(descr, prog->description);
}

/*
 *
 */
void
mvp_tvguide_hide(void *proglist, void *descr, void * clock)
{
	mvpw_hide((mvp_widget_t *)clock);
	mvpw_hide((mvp_widget_t *)descr);
	mvpw_hide((mvp_widget_t *)proglist);
}

/*
 * Based on the integer passed in, return the index into the
 * provided chanlist array or return -1 if a match for the channel
 * number and callsign.
 */
int
myth_get_chan_index_from_int(cmyth_chanlist_t chanlist, int nchan)
{
	int rtrn;

	for(rtrn = 0; rtrn < chanlist->chanlist_count; rtrn++)
		if(chanlist->chanlist_list[rtrn].channum >= nchan)
			break;
	rtrn = rtrn==chanlist->chanlist_count?-1:rtrn;

	return rtrn;
}

/*
 * Based on the string passed in, return the index into the
 * provided chanlist array or return -1 if a match for the channel
 * number and callsign.
 */
int
myth_get_chan_index_from_str(cmyth_chanlist_t chanlist, char * chan)
{
	int rtrn;
	int nchan = atoi(chan);

	for(rtrn = 0; rtrn < chanlist->chanlist_count; rtrn++)
		if(chanlist->chanlist_list[rtrn].channum >= nchan)
			break;
	rtrn = rtrn==chanlist->chanlist_count?-1:rtrn;

	return rtrn;
}

/*
 * Based on the proginfo passed in, return the index into the
 * provided chanlist array or return -1 if a match for the channel
 * number and callsign.
 */
int
myth_get_chan_index(cmyth_chanlist_t chanlist, cmyth_proginfo_t prog)
{
	int rtrn;

	for(rtrn = 0; rtrn < chanlist->chanlist_count; rtrn++)
		if(chanlist->chanlist_list[rtrn].chanid == prog->proginfo_chanId
			 && strcmp(chanlist->chanlist_list[rtrn].callsign,
			 					 prog->proginfo_chansign) == 0)
			break;
	rtrn = rtrn==chanlist->chanlist_count?-1:rtrn;

	return rtrn;
}

/*
 *
 */
static int
get_chan_num(long chanid, cmyth_chanlist_t chanlist)
{
	int i;

	for(i=0; i<chanlist->chanlist_count;i++) {
		if(chanlist->chanlist_list[i].chanid == chanid)
			return chanlist->chanlist_list[i].channum;
	}

	return 0;
}

/*
 *
 */
int
get_tvguide_selected_channel(mvp_widget_t *proglist)
{
	cmyth_tvguide_program_t prog;

	prog = (cmyth_tvguide_program_t)
 	mvpw_get_array_cur_cell_data(proglist);

	PRINTF("** SSDEBUG: Current prog showing as: %s\n", prog->title);

	return prog->channum;
}
/*
 *
 */
static cmyth_tvguide_progs_t
get_tvguide_page(MYSQL *mysql, cmyth_chanlist_t chanlist,
								 cmyth_tvguide_progs_t proglist, int index,
								 struct tm * start_time, struct tm * end_time) 
{
	MYSQL_RES *res=NULL;
	MYSQL_ROW row;
  char query[350];
	char startdate[16];
	char enddate[16];
	char starttime[25];
	char endtime[25];
	char channels[50];
	int i, rows = 0, idxs[4], idx=0; 
	static cmyth_tvguide_program_t cache = NULL;
	int cache_ct;
	long ch=0;

	if(!cache)
		cache = cmyth_allocate(sizeof(*cache)*4);


	PRINTF("** SSDEBUG: index is: %d\n", index);
	idxs[0] = index < 0 ? chanlist->chanlist_count+index:index;
	index--;
	PRINTF("** SSDEBUG: index is: %d\n", index);
	idxs[1] = index < 0 ? chanlist->chanlist_count+index:index;
	index--;
	PRINTF("** SSDEBUG: index is: %d\n", index);
	idxs[2] = index < 0 ? chanlist->chanlist_count+index:index;
	index--;
	PRINTF("** SSDEBUG: index is: %d\n", index);
	idxs[3] = index < 0 ? chanlist->chanlist_count+index:index;

	PRINTF("** SSDEBUG: indexes are: %d, %d, %d, %d\n", idxs[0], idxs[1],
					idxs[2], idxs[3]);
	PRINTF("** SSDEBUG: callsigns are: %s, %s, %s, %s\n",
		chanlist->chanlist_list[idxs[0]].callsign,
		chanlist->chanlist_list[idxs[1]].callsign,
		chanlist->chanlist_list[idxs[2]].callsign,
		chanlist->chanlist_list[idxs[3]].callsign
	);

	sprintf(channels, "(%ld, %ld, %ld, %ld)",
		chanlist->chanlist_list[idxs[0]].chanid,
		chanlist->chanlist_list[idxs[1]].chanid,
		chanlist->chanlist_list[idxs[2]].chanid,
		chanlist->chanlist_list[idxs[3]].chanid
	);

	strftime(starttime, 25, "%F %T", start_time);
	strftime(endtime, 25, "%F %T", end_time);

	PRINTF("** SSDEBUG: starttime:%s, endtime:%s\n", starttime, endtime);

	sprintf(query, 
		"SELECT chanid,starttime,endtime,title,description,\
						subtitle,programid,seriesid,category \
						FROM program \
						WHERE starttime<'%s' \
							AND endtime>'%s' \
							AND chanid in %s \
							ORDER BY chanid DESC, \
							starttime ASC", endtime,starttime, channels);
	cmyth_dbg(CMYTH_DBG_ERROR, "%s: query= %s\n", __FUNCTION__, query);
	if(mysql_query(mysql,query)) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                           __FUNCTION__, mysql_error(mysql));
		mysql_close(mysql);
		return NULL;
	}
	res = mysql_store_result(mysql);


	
	PRINTF("** SSDEBUG: got %llu rows from query\n", res->row_count);
	/*
	 * Need to do some special handling on the query results. Sometimes
	 * no data exists for certain channels as a result of the query
	 * and we need to fill it in with unknown. In other cases, there
	 * are a series of 10 min shows in a 30 min period so there may
	 * be more than one so only the first one should be used. Also
	 * in the case we're wrapping around the rows are returned from
	 * the query in the wrong order so we need to suck in the entire
	 * query result and search for our specific line in it.
	 */

	idx = 0;
	while((row = mysql_fetch_row(res))) {
		ch = atol(row[0]);
		cache[idx].channum = get_chan_num(ch, chanlist);

		if(idx > 0 && ch == cache[idx-1].chanid) {
			PRINTF("** SSDEBUG: Cache discarding entry with same chanid in same slot\n");
			continue;
		}
		PRINTF("** SSDEBUG: cache: row: %d, %ld, %d, %s, %s, %s\n", idx, ch,
						cache[idx].channum, row[3], row[5], row[8]);
		cache[idx].chanid=ch;
		cache[idx].recording=0;
		strncpy ( cache[idx].starttime, row[1], 25);
		strncpy ( cache[idx].endtime, row[2], 25);
		strncpy ( cache[idx].title, row[3], 130);
		strncpy ( cache[idx].description, row[4], 256);
		strncpy ( cache[idx].subtitle, row[5], 130);
		strncpy ( cache[idx].programid, row[6], 20);
		strncpy ( cache[idx].seriesid, row[7], 12);
		strncpy ( cache[idx].category, row[8], 64);
		idx++;
	}
	cache_ct = idx;
  mysql_free_result(res);


	/* Query the scheduled programs in this timeframe */
	strftime(startdate, 16, "%F", start_time);
	strftime(enddate, 16, "%F", end_time);
	strftime(starttime, 25, "%T", start_time);
	strftime(endtime, 25, "%T", end_time);

	sprintf(query,
		"SELECT chanid, programid \
		 FROM record \
		 WHERE startdate = '%s' \
		 AND enddate = '%s' \
		 AND starttime < '%s' \
		 AND endtime > '%s' \
		 AND chanid IN %s",
		 startdate, enddate, endtime, starttime, channels);
	cmyth_dbg(CMYTH_DBG_ERROR, "%s: query= %s\n", __FUNCTION__, query);
	if(mysql_query(mysql,query)) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                           __FUNCTION__, mysql_error(mysql));
		mysql_close(mysql);
		return NULL;
	}

	/* Now flag all programs schduled to record */
	res = mysql_store_result(mysql);
	PRINTF("** SSDEBUG: got %llu rows from query\n", res->row_count);
	while((row = mysql_fetch_row(res))) {
		ch = atol(row[0]);
		PRINTF("** SSDEBUG: chanid returned is %ld\n", ch);
		for(i = 0; i<cache_ct; i++) {
			PRINTF("** SSDEBUG: cache chanid is %ld\n", cache[i].chanid);
			if(cache[i].chanid == ch && strcmp(row[1], cache[i].programid) == 0) {
				PRINTF("** SSDEBUG: chanid match on %ld\n", ch);
				cache[i].recording = 1;
			}
		}
	}
  mysql_free_result(res);

	rows = proglist->count;
	for(idx=0;idx<4;idx++) {


		for(i = 0; i<cache_ct; i++) {
			if(cache[i].chanid == chanlist->chanlist_list[idxs[idx]].chanid) {
				break;
			}
		}
		
		/*
		if(cache[i].chanid != chanlist->chanlist_list[idxs[idx]].chanid) {
		*/
		if(i == cache_ct) {
			PRINTF("** SSDEBUG: no program info on channel id: %d between %s, %s\n",
							idxs[idx], starttime, endtime);
			proglist->progs[rows].channum = get_chan_num(idxs[idx], chanlist);
			proglist->progs[rows].chanid=chanlist->chanlist_list[idxs[idx]].chanid;
			proglist->progs[rows].recording=cache[i].recording;
			strncpy ( proglist->progs[rows].starttime, starttime, 25);
			strncpy ( proglist->progs[rows].endtime, endtime, 25);
			strncpy ( proglist->progs[rows].title, "Unknown", 130);
			strncpy ( proglist->progs[rows].description, 
				"There are no entries in the database for this channel at this time",
			 	256);
			strncpy ( proglist->progs[rows].subtitle, "Unknown", 130);
			strncpy ( proglist->progs[rows].programid, "Unknown", 20);
			strncpy ( proglist->progs[rows].seriesid, "Unknown", 12);
			strncpy ( proglist->progs[rows].category, "Unknown", 64);
		}
		else { /* All aligns, move the information */
			proglist->progs[rows].channum = get_chan_num(cache[i].chanid, chanlist);
			PRINTF("** SSDEBUG: row: %d, %ld, %d, %s, %s, %s\n", rows,
							cache[i].chanid, proglist->progs[rows].channum,
							cache[i].title, cache[i].subtitle, cache[i].category);
			proglist->progs[rows].chanid=cache[i].chanid;
			proglist->progs[rows].recording=cache[i].recording;
			strncpy ( proglist->progs[rows].starttime, cache[i].starttime, 25);
			strncpy ( proglist->progs[rows].endtime, cache[i].endtime, 25);
			strncpy ( proglist->progs[rows].title, cache[i].title, 130);
			strncpy ( proglist->progs[rows].description, cache[i].description, 256);
			strncpy ( proglist->progs[rows].subtitle, cache[i].subtitle, 130);
			strncpy ( proglist->progs[rows].programid, cache[i].programid, 20);
			strncpy ( proglist->progs[rows].seriesid, cache[i].seriesid, 12);
			strncpy ( proglist->progs[rows].category, cache[i].category, 64);
			cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].chanid =  %d\n",rows,
								proglist->progs[rows].chanid);
			cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].title =  %s\n",rows,
								proglist->progs[rows].title);
		}
		rows++;
	}
	proglist->count = rows;

	return proglist;
}

int
myth_guide_set_channels(void * widget, cmyth_chanlist_t chanlist,
												int index, int yofs,
												long free_recorders)
{
	int i,j, rtrn;
	char buf[64];
	mvp_widget_t * prog_widget = (mvp_widget_t *) widget;

	PRINTF("** SSDEBUG: request to load row labels: %d\n", index);

	index += yofs;

	index = index >= chanlist->chanlist_count
					?index-chanlist->chanlist_count:index;
	index = index + chanlist->chanlist_count < 0
					? index+chanlist->chanlist_count:index;
	
	rtrn = index;

	PRINTF("** SSDEBUG: index is %d\n", index);

	/*
	 * Set the four visible channels in the widget
	 */
	for(i = index; i>index-4; i--) {
		if(i <0) {
			j = chanlist->chanlist_count + i;
			sprintf(buf, "%d\n%s", chanlist->chanlist_list[j].channum,
						chanlist->chanlist_list[j].callsign);
			if((free_recorders & chanlist->chanlist_list[j].cardids) == 0)
				mvpw_set_array_row_bg(prog_widget, index-i, MVPW_DARK_RED);
			else
				mvpw_set_array_row_bg(prog_widget, index-i, MVPW_DARKGREY);
			mvpw_set_array_row(prog_widget, index-i, buf, NULL);
			PRINTF("** SSDEBUG: loading guide: %d:%s\n",
						chanlist->chanlist_list[j].channum,
						chanlist->chanlist_list[j].callsign);
		}
		else {
			sprintf(buf, "%d\n%s", chanlist->chanlist_list[i].channum,
						chanlist->chanlist_list[i].callsign);
			if((free_recorders & chanlist->chanlist_list[i].cardids) == 0)
				mvpw_set_array_row_bg(prog_widget, index-i, MVPW_DARK_RED);
			else
				mvpw_set_array_row_bg(prog_widget, index-i, MVPW_DARKGREY);
			mvpw_set_array_row(prog_widget, index-i, buf, NULL);
			PRINTF("** SSDEBUG: loading guide: %d:%s\n",
						chanlist->chanlist_list[i].channum,
						chanlist->chanlist_list[i].callsign);
		}
	}

	return rtrn;
}

/*
 * For testing, this function just loads the view that we need to
 * look at.
 */
cmyth_tvguide_progs_t
myth_load_guide(void * widget, cmyth_database_t db,
											 cmyth_chanlist_t chanlist,
											 cmyth_tvguide_progs_t proglist,
											 int index, int * xofs, int * yofs,
											 long free_recorders)
{
	MYSQL *mysql;
	int i, j, k, m, prev;
	time_t curtime, nexttime;
	struct  tm * tmp, now, later;
	cmyth_tvguide_progs_t rtrn = proglist;
	cmyth_tvguide_program_t prog;

	PRINTF("** SSDEBUG: request to load guide: %d\n", index);

	/* Handle wraparound properly */
	(*yofs) = (*yofs)%chanlist->chanlist_count;

	index = myth_guide_set_channels(widget, chanlist, index, *yofs,
																	free_recorders);

	/* Allocate a new proglist if required TODO, this needs to be
	 * changed to use the standard methodology.
	 */
	if(!proglist) {
		proglist = (cmyth_tvguide_progs_t) cmyth_allocate(sizeof(*proglist));
		proglist->progs =
			cmyth_allocate(sizeof(struct cmyth_tvguide_program) * 3 * 4);
		proglist->count = 0;
		proglist->alloc = 0;
	}
	if(proglist->progs) {
		proglist->count = 0;
	}

  mysql=mysql_init(NULL);
	if(!(mysql_real_connect(mysql,db->db_host,db->db_user,
													db->db_pass,db->db_name,0,NULL,0))) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_connect() Failed: %s\n",
                           __FUNCTION__, mysql_error(mysql));
		fprintf(stderr, "mysql_connect() Failed: %s\n",mysql_error(mysql));
        	mysql_close(mysql);
		return NULL;
	}

	curtime = time(NULL);
	curtime += 60*30*(*xofs);
#ifdef MERGE_CELLS_dont
	mvpw_reset_array_cells(widget);
#endif
	for(j=0;j<3;j++) {
		tmp = localtime(&curtime);
		memcpy(&now, tmp, sizeof(struct tm));
		now.tm_min = now.tm_min >= 30?30:0;
		now.tm_sec = 0;
		curtime = mktime(&now);
		nexttime = curtime + 60*30;
		tmp = localtime(&nexttime);
		memcpy(&later, tmp, sizeof(struct tm));
		later.tm_min = later.tm_min >= 30?30:0;
		later.tm_sec = 0;
		prev = proglist->count;
		PRINTF("** SSEDBUG: Calling set_guide_mysql2\n");
		rtrn  = get_tvguide_page(mysql, chanlist, proglist, index, &now, &later);
		PRINTF("** SSEDBUG: done set_guide_mysql2 rtrn = %p\n", rtrn);
		if(rtrn == NULL)
			return proglist;

		k=0;
		for(i=prev; i<rtrn->count; i++) {
			if(i>prev && rtrn->progs[i].chanid == rtrn->progs[i-1].chanid) {
				k++;
				continue;
			}
			PRINTF("** SSDEBUG: Loaded prog(%d): %d, %ld, %d, %s, %s, %s, %s, %s, %s, %s, %s\n",
			i,
			rtrn->progs[i].channum,
			rtrn->progs[i].chanid,
			rtrn->progs[i].recording,
			rtrn->progs[i].starttime,
			rtrn->progs[i].endtime,
			rtrn->progs[i].title,
			rtrn->progs[i].description,
			rtrn->progs[i].subtitle,
			rtrn->progs[i].programid,
			rtrn->progs[i].seriesid,
			rtrn->progs[i].category);
			/* Determine if the left neighbour is the same show and if so just
			 * extend the previous cell instead of filling the current one in
			 */
			/* Fill in the info in the guide */
#ifdef MERGE_CELLS
			for(m=0; m<j; m++) {
					prog = (struct cmyth_tvguide_program *)
											mvpw_get_array_cell_data(widget, m, i-prev-k);
					if(strcmp(prog->starttime, rtrn->progs[i].starttime) == 0
					 	&& strcmp(prog->endtime, rtrn->progs[i].endtime) == 0 ) {
						/*
						PRINTF("** SSDEBUG: Need collapse %d cells for %s\n",
							j-m+1, prog->title);
						*/
						break;
					}
			}
			mvpw_set_array_cell_span(widget, m, i-prev-k, j-m+1);
#endif
			if(rtrn->progs[i].recording) {
				mvpw_set_array_cell_theme(widget, j, i-prev-k, &mvpw_record_theme);
				PRINTF("** SSDEBUG: setting cell color for cell %d, %d\n",
							 j, i-prev-k);
			}
			else {
				mvpw_set_array_cell_theme(widget, j, i-prev-k, NULL);
			}

			mvpw_set_array_cell_data(widget, j, i-prev-k, &rtrn->progs[i]);
			mvpw_set_array_cell(widget, j, i-prev-k, rtrn->progs[i].title, NULL);
		}
		curtime = nexttime;
		/*
		PRINTF("** SSDEBUG: Looping next column\n");
		*/
	}
	mvpw_array_clear_dirty(widget);

  mysql_close(mysql);
	
	return rtrn;
}

/*
 *
 */
int
myth_set_guide_times(void * widget, int xofs)
{
	mvp_widget_t * prog_widget = (mvp_widget_t *) widget;
	struct tm *ltime;
	char timestr[25];
	time_t curtime, nexthr;
	static int last_minutes = -1;
	static int last_ofs = 0;
	int minutes, rtrn=1;

	curtime = time(NULL);
	curtime += 60*30*xofs;
	ltime = localtime(&curtime);
	/*
	strftime(timestr, 25, "%M", ltime);
	minutes = atoi(timestr);
	*/
	minutes = ltime->tm_min;
	if(last_minutes == -1
	|| (last_minutes < 30 && minutes >= 30)
	|| minutes < last_minutes
	|| last_ofs != xofs) {
		last_minutes = minutes;
		last_ofs = xofs;
		strftime(timestr, 25, "%b/%d", ltime);
		mvpw_set_array_col(prog_widget, 0, timestr, NULL);
		if(minutes < 30) {
			strftime(timestr, 25, "%H:00", ltime);
			mvpw_set_array_col(prog_widget, 1, timestr, NULL);
			strftime(timestr, 25, "%H:30", ltime);
			mvpw_set_array_col(prog_widget, 2, timestr, NULL);
			nexthr = curtime + 60*60;
			ltime = localtime(&nexthr);
			strftime(timestr, 25, "%H:00", ltime);
			mvpw_set_array_col(prog_widget, 3, timestr, NULL);
		}
		else {
			strftime(timestr, 25, "%H:30", ltime);
			mvpw_set_array_col(prog_widget, 1, timestr, NULL);
			nexthr = curtime + 60*60;
			ltime = localtime(&nexthr);
			strftime(timestr, 25, "%H:00", ltime);
			mvpw_set_array_col(prog_widget, 2, timestr, NULL);
			strftime(timestr, 25, "%H:30", ltime);
			mvpw_set_array_col(prog_widget, 3, timestr, NULL);
		}
	}
	else
		rtrn = 0;
	
	mvpw_array_clear_dirty(widget);

	return rtrn;
}

cmyth_chanlist_t
myth_release_chanlist(cmyth_chanlist_t cl)
{
	int i;

	if(cl) {
		for(i=0; i<cl->chanlist_count; i++) {
			cmyth_release(cl->chanlist_list[i].callsign);
			cmyth_release(cl->chanlist_list[i].name);
		}
		cmyth_release(cl);
	}
	return NULL;
}

cmyth_tvguide_progs_t
myth_release_proglist(cmyth_tvguide_progs_t proglist)
{
	cmyth_release(proglist->progs);
	cmyth_release(proglist);
	return NULL;
}

#define MAX_TUNER 16
long
myth_tvguide_get_free_cardids(cmyth_conn_t control)
{
	long rtrn = 0;
	int i;
	cmyth_conn_t ctrl = cmyth_hold(control);
	cmyth_recorder_t rec;
	static int last_tuner = MAX_TUNER;

	for (i=1; i<last_tuner; i++) {
		/*
		fprintf(stderr, "Looking for recorder %d\n", i);
		*/
		if ((rec = cmyth_conn_get_recorder_from_num(ctrl,i)) == NULL) {
			last_tuner = i;
			break;
		}
		if(cmyth_recorder_is_recording(rec) != 1) {
			rtrn |= 1<<(i-1);
			/*
			PRINTF("** SSDEBUG recorder %d is free\n", i);
			*/
		}
		cmyth_release(rec);
	}
	cmyth_release(ctrl);


	return rtrn;
}

long
myth_tvguide_get_active_card(cmyth_recorder_t rec)
{
	long rtrn = 0;

	if(rec)
		rtrn |= 1<<(rec->rec_id-1);
	
	/*
	PRINTF("** SSDEBUG recorder bitmap %ld is our active device\n", rtrn);
	*/

	return rtrn;
}

/*
 *
 */
cmyth_chanlist_t
myth_load_channels2(cmyth_database_t db)
{
	MYSQL *mysql;
	MYSQL_RES *res=NULL;
	MYSQL_ROW row;
	char query[300];
	cmyth_chanlist_t rtrn;
        
	mysql=mysql_init(NULL);
	PRINTF("** SSDEBUG host:%s, user:%s, password:%s\n", db->db_host,
				 db->db_user, db->db_pass);
	if(!(mysql_real_connect(mysql,db->db_host,db->db_user, db->db_pass,
													db->db_name, 0,NULL,0))) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_connect() Failed: %s\n",
    					__FUNCTION__, mysql_error(mysql));
		fprintf(stderr, "mysql_connect() Failed: %s\n",mysql_error(mysql));
						mysql_close(mysql);
		return NULL;
	}
	/*
	 * A table join query is required to generate the bitstring that
	 * represents the recorders that can be used to access the channel.
	 */
	sprintf(query, "SELECT chanid,channum,channum+0 as channumi,cardid, \
									callsign,name \
									FROM cardinput, channel \
									WHERE cardinput.sourceid=channel.sourceid \
									AND visible=1 \
									ORDER BY channumi,callsign ASC");
	cmyth_dbg(CMYTH_DBG_ERROR, "%s: query= %s\n", __FUNCTION__, query);

	if(mysql_query(mysql,query)) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                           __FUNCTION__, mysql_error(mysql));
		mysql_close(mysql);
		return NULL;
	}

	res = mysql_store_result(mysql);
	PRINTF("** SSDEBUG: Number of rows retreived = %llu\n", res->row_count);
	/* Create a return structure that has room for all the records
	 * retrieved knowing that some may be the same on multiple recorders
	 */
	rtrn = cmyth_allocate(sizeof(*rtrn));
	rtrn->chanlist_list = (cmyth_channel_t)
			cmyth_allocate(sizeof(struct cmyth_channel)*res->row_count/ALLOC_FRAC);
	if(!rtrn->chanlist_list) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: chanlist allocation failed\n", 
							__FUNCTION__);
		return NULL;
	}
	rtrn->chanlist_alloc = res->row_count/ALLOC_FRAC;
	rtrn->chanlist_count = 0;

	while((row = mysql_fetch_row(res))) {
		if(rtrn->chanlist_count == rtrn->chanlist_alloc) {
			PRINTF("** SSDEBUG: allocating more space with count = %d and alloc =%d\n",	rtrn->chanlist_count, rtrn->chanlist_alloc);

			rtrn->chanlist_list = (cmyth_channel_t)
				cmyth_reallocate(rtrn->chanlist_list, sizeof(struct cmyth_channel)
										*(rtrn->chanlist_count + res->row_count/ALLOC_FRAC));
			if(!rtrn->chanlist_list) {
				cmyth_dbg(CMYTH_DBG_ERROR, "%s: chanlist allocation failed\n", 
									__FUNCTION__);
				return NULL;
			}
			rtrn->chanlist_alloc += res->row_count/ALLOC_FRAC;
		}
		/* Check if this entry is the same as the previous. If it is, then
		 * the only differentiation is the recorder. Add it to the sources
		 * for the previous recorder and ignore this one.
		 */
		if(rtrn->chanlist_count &&
			!strcmp(rtrn->chanlist_list[rtrn->chanlist_count-1].callsign, row[4])
			&& (rtrn->chanlist_list[rtrn->chanlist_count-1].channum == atoi(row[1]))
			&& !strcmp(rtrn->chanlist_list[rtrn->chanlist_count-1].name, row[5])) {
			rtrn->chanlist_list[rtrn->chanlist_count-1].cardids
																			|= 1 << (atoi(row[3])-1);
		}
		else {
			rtrn->chanlist_list[rtrn->chanlist_count].chanid = atoi(row[0]);
			rtrn->chanlist_list[rtrn->chanlist_count].channum = atoi(row[1]);
			rtrn->chanlist_list[rtrn->chanlist_count].cardids = 1 << (atoi(row[3])-1);
			rtrn->chanlist_list[rtrn->chanlist_count].callsign = cmyth_strdup(row[4]);
			rtrn->chanlist_list[rtrn->chanlist_count].name = cmyth_strdup(row[5]);
			rtrn->chanlist_count += 1;
		}
		/*
		PRINTF("** SSDEBUG: cardid for channel %d is %ld with count %d\n",
			rtrn->chanlist_list[rtrn->chanlist_count-1].channum,
			rtrn->chanlist_list[rtrn->chanlist_count-1].cardids,
			rtrn->chanlist_count-1);
		*/
	}
	cmyth_dbg(CMYTH_DBG_ERROR, "%s returned rows =  %d\n",__FUNCTION__,
						res->row_count);
	mysql_free_result(res);
	mysql_close(mysql);
	return rtrn;
}
