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
mvp_tvguide_show(mvp_widget_t *proglist, mvp_widget_t *descr)
{
	cmyth_tvguide_program_t prog;

	mvpw_show(descr);
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
mvp_tvguide_hide(void *proglist, void *descr)
{
	mvpw_hide((mvp_widget_t *)descr);
	mvpw_hide((mvp_widget_t *)proglist);
}

/*
 * Based on the proginfo passed in, return the index into the
 * provided chanlist array or return -1 if a match for the channel
 * number and callsign.
 */
int
myth_get_chan_index_from_str(cmyth_chanlist_t chanlist, char * chan)
{
	int rtrn;
	int nchan = atoi(chan);

	for(rtrn = 0; rtrn < chanlist->chanlist_count; rtrn++)
		if(chanlist->chanlist_list[rtrn].channum == nchan)
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

	printf("** SSDEBUG: Current prog showing as: %s\n", prog->title);

	return prog->channum;
}
/*
 *
 */
static cmyth_tvguide_progs_t
/*
get_guide_mysql2(cmyth_database_t db, cmyth_chanlist_t chanlist,
*/
get_guide_mysql2(MYSQL *mysql, cmyth_chanlist_t chanlist,
								 cmyth_tvguide_progs_t proglist, int index,
								 struct tm * start_time, struct tm * end_time) 
{
	/*
	MYSQL *mysql;
	*/
	MYSQL_RES *res=NULL;
	MYSQL_ROW row;
  char query[350];
	char starttime[25];
	char endtime[25];
	char channels[50];
	int rows = 0, i1,i2,i3,i4; 
	long ch;


	strftime(starttime, 25, "%F %T", start_time);
	strftime(endtime, 25, "%F %T", end_time);

	i1 = index < 0 ? chanlist->chanlist_count+index:index;
	index--;
	i2 = index < 0 ? chanlist->chanlist_count+index:index;
	index--;
	i3 = index < 0 ? chanlist->chanlist_count+index:index;
	index--;
	i4 = index < 0 ? chanlist->chanlist_count+index:index;
	/*
	printf("** SSDEBUG: indexes are: %d, %d, %d, %d\n", i1, i2, i3, i4);
	*/
	sprintf(channels, "(%ld, %ld, %ld, %ld)",
		chanlist->chanlist_list[i1].chanid,
		chanlist->chanlist_list[i2].chanid,
		chanlist->chanlist_list[i3].chanid,
		chanlist->chanlist_list[i4].chanid
	);
	/*
	printf("** SSDEBUG: starttime:%s, endtime:%s\n", starttime, endtime);
	printf("** SSDEBUG: database pointer: %p\n", db);
  mysql=mysql_init(NULL);
	if(!(mysql_real_connect(mysql,db->db_host,db->db_user,
													db->db_pass,db->db_name,0,NULL,0))) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_connect() Failed: %s\n",
                           __FUNCTION__, mysql_error(mysql));
		fprintf(stderr, "mysql_connect() Failed: %s\n",mysql_error(mysql));
        	mysql_close(mysql);
		return NULL;
	}
	printf("** SSDEBUG: database connected\n");
	*/
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

	
	/*
	printf("** SSDEBUG: got %llu rows from query\n", res->row_count);
	*/
	if(!proglist->progs) {
		proglist->progs =
			cmyth_allocate(sizeof(struct cmyth_tvguide_program) * res->row_count);
		printf("** SSDEBUG: allocated proglist->progs: %p\n", proglist->progs);
	}
	else {
		if(proglist->alloc < res->row_count+proglist->count) {
			proglist->progs =
			cmyth_reallocate(proglist->progs, sizeof(struct cmyth_tvguide_program) *
										 	(res->row_count+proglist->count));
			proglist->alloc = res->row_count+proglist->count;
			printf("** SSDEBUG: reallocated proglist->progs: %p\n", proglist->progs);
			printf("** SSDEBUG: reallocated %llu items\n",
							res->row_count+proglist->count);
		}
		rows = proglist->count;
	}
	while((row = mysql_fetch_row(res))) {

		ch = atol(row[0]);
		proglist->progs[rows].channum = get_chan_num(ch, chanlist);
		/*
		printf("** SSDEBUG: row: %d, %ld, %d, %s, %s, %s\n", rows, ch,
						proglist->progs[rows].channum, row[3], row[5], row[8]);
		*/
		proglist->progs[rows].chanid=ch;
		proglist->progs[rows].recording=0;
		strcpy ( proglist->progs[rows].starttime, row[1]);
		strcpy ( proglist->progs[rows].endtime, row[2]);
		strcpy ( proglist->progs[rows].title, row[3]);
		strcpy ( proglist->progs[rows].description, row[4]);
		strcpy ( proglist->progs[rows].subtitle, row[5]);
		strcpy ( proglist->progs[rows].programid, row[6]);
		strcpy ( proglist->progs[rows].seriesid, row[7]);
		strcpy ( proglist->progs[rows].category, row[8]);
		cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].chanid =  %d\n",rows,
							proglist->progs[rows].chanid);
		cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].title =  %s\n",rows,
							proglist->progs[rows].title);
		rows++;
	}
	proglist->count = rows;
  mysql_free_result(res);
	/*
  mysql_close(mysql);
	*/
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

	/*
	printf("** SSDEBUG: request to load row labels: %d\n", index);
	*/

	index += yofs;

	index = index > chanlist->chanlist_count
					?index-chanlist->chanlist_count:index;
	index = index + chanlist->chanlist_count < 0
					? index+chanlist->chanlist_count:index;
	
	rtrn = index;

	/*
	 * Set the four visible channels in the widget
	 */
	for(i = index; i>index-4; i--) {
		if(i <0) {
			j = chanlist->chanlist_count - i - 1;
			sprintf(buf, "%d\n%s", chanlist->chanlist_list[j].channum,
						chanlist->chanlist_list[j].callsign);
			if((free_recorders & chanlist->chanlist_list[j].cardids) == 0)
				mvpw_set_array_row_bg(prog_widget, index-i, MVPW_DARK_RED);
			else
				mvpw_set_array_row_bg(prog_widget, index-i, MVPW_DARKGREY);
			mvpw_set_array_row(prog_widget, index-i, buf, NULL);
			/*
			printf("** SSDEBUG: loading guide: %d:%s\n",
						chanlist->chanlist_list[j].channum,
						chanlist->chanlist_list[j].callsign);
			*/
		}
		else {
			sprintf(buf, "%d\n%s", chanlist->chanlist_list[i].channum,
						chanlist->chanlist_list[i].callsign);
			if((free_recorders & chanlist->chanlist_list[i].cardids) == 0)
				mvpw_set_array_row_bg(prog_widget, index-i, MVPW_DARK_RED);
			else
				mvpw_set_array_row_bg(prog_widget, index-i, MVPW_DARKGREY);
			mvpw_set_array_row(prog_widget, index-i, buf, NULL);
			/*
			printf("** SSDEBUG: loading guide: %d:%s\n",
						chanlist->chanlist_list[i].channum,
						chanlist->chanlist_list[i].callsign);
			*/
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
											 int index, int xofs, int yofs,
											 long free_recorders)
{
	MYSQL *mysql;
	int i, j, k, m, prev;
	time_t curtime, nexttime;
	struct  tm * tmp, now, later;
	cmyth_tvguide_progs_t rtrn = proglist;
	struct cmyth_tvguide_program * prog;

	/*
	printf("** SSDEBUG: request to load guide: %d\n", index);
	*/

	index = myth_guide_set_channels(widget, chanlist, index, yofs,
																	free_recorders);

	/* Allocate a new proglist if required TODO, this needs to be
	 * changed to use the standard methodology.
	 */
	if(!proglist) {
		proglist = (cmyth_tvguide_progs_t) cmyth_allocate(sizeof(*proglist));
		proglist->progs = NULL;
		proglist->count = 0;
		proglist->alloc = 0;
	}
	if(proglist->progs) {
		/* Right now, if we don't do this, memory gets so badly fragmented
		 * that we run out of any usable space (or at least that's how it
		 * appears when the system crashes since addresses seem to exceed
		 * the end of available memory). We should be able to just set the
		 * count to 0 and allow re-allocation to take place but this just
		 * eats up contiguous memory chunks until we crash. It seems to
		 * happen most redily when channels are changed often in a single
		 * session but no memory issues occur if we release and re-allocate
		 * each time. Not to say that there isn't some other bug in the live
		 * tv implementation that might be causing this but code inspections
		 * haven't yeilded any potential suspects.
		cmyth_release(proglist->progs);
		proglist->progs = NULL;
		 */
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
	curtime += 60*30*xofs;
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
		/*
		printf("** SSEDBUG: Calling set_guide_mysql2\n");
		*/
		rtrn  = get_guide_mysql2(mysql, chanlist, proglist, index, &now, &later);
		/*
		printf("** SSEDBUG: done set_guide_mysql2 rtrn = %p\n", rtrn);
		*/
		if(rtrn == NULL)
			return proglist;

		k=0;
		for(i=prev; i<rtrn->count; i++) {
			if(i>prev && rtrn->progs[i].chanid == rtrn->progs[i-1].chanid) {
				k++;
				continue;
			}
			/*
			printf("** SSDEBUG: Loaded prog: %d, %ld, %d, %s, %s, %s, %s, %s, %s, %s, %s\n",
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
			*/
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
						printf("** SSDEBUG: Need collapse %d cells for %s\n",
							j-m+1, prog->title);
						*/
						break;
					}
			}
			mvpw_set_array_cell_span(widget, m, i-prev-k, j-m+1);
#endif
			mvpw_set_array_cell_data(widget, j, i-prev-k, &rtrn->progs[i]);
			mvpw_set_array_cell(widget, j, i-prev-k, rtrn->progs[i].title, NULL);
		}
		curtime = nexttime;
		/*
		printf("** SSDEBUG: Looping next column\n");
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
			printf("** SSDEBUG recorder %d is free\n", i);
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
	printf("** SSDEBUG recorder bitmap %ld is our active device\n", rtrn);
	*/

	return rtrn;
}

/*
 *
 */
cmyth_chanlist_t
myth_load_channels2(cmyth_database_t db, pthread_mutex_t * mutex)
{
	MYSQL *mysql;
	MYSQL_RES *res=NULL;
	MYSQL_ROW row;
	char query[256];
	cmyth_chanlist_t rtrn;
        
	mysql=mysql_init(NULL);
	printf("** SSDEBUG host:%s, user:%s, password:%s\n", db->db_host,
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
									ORDER BY channumi,callsign ASC");
/*
	sprintf(query, "SELECT chanid,channum,channum+0 as channumi,sourceid, \
									callsign,name \
									FROM channel \
									ORDER BY channumi,callsign ASC");
*/
	cmyth_dbg(CMYTH_DBG_ERROR, "%s: query= %s\n", __FUNCTION__, query);

	if(mysql_query(mysql,query)) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                           __FUNCTION__, mysql_error(mysql));
		mysql_close(mysql);
		return NULL;
	}
	/* Allow others to run since this eats lots of cycles */
	pthread_mutex_unlock(mutex);
	pthread_mutex_lock(mutex);
	res = mysql_store_result(mysql);
	printf("** SSDEBUG: Number of rows retreived = %llu\n", res->row_count);
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
			printf("** SSDEBUG: allocating more space with count = %d and alloc =%d\n",	rtrn->chanlist_count, rtrn->chanlist_alloc);
			/* Allow others to run since this eats lots of cycles */
			/*
			pthread_mutex_unlock(mutex);
			pthread_mutex_lock(mutex);
			*/
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
		 * for the previous record and ignore this one.
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
		printf("** SSDEBUG: cardid for channel %d is %ld with count %d\n",
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
