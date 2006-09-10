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

#if 0
#define PRINTF(x...) PRINTF(x)
#define TRC(fmt, args...) PRINTF(fmt, ## args) 
#else
#define PRINTF(x...)
#define TRC(fmt, args...) 
#endif

void
cmyth_database_close(cmyth_database_t db)
{
    if(db->mysql != NULL)
    {
	mysql_close(db->mysql);
	db->mysql = NULL;
    }
}

cmyth_database_t
cmyth_database_init(char *host, char *db_name, char *user, char *pass)
{
	cmyth_database_t rtrn = cmyth_allocate(sizeof(*rtrn));
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s\n", __FUNCTION__);

	if (rtrn != NULL) {
	    rtrn->db_host = cmyth_strdup(host);
	    rtrn->db_user = cmyth_strdup(user);
	    rtrn->db_pass = cmyth_strdup(pass);
	    rtrn->db_name = cmyth_strdup(db_name);
	}

	return rtrn;
}

int
cmyth_database_set_host(cmyth_database_t db, char *host)
{
	PRINTF("** SSDEBUG: setting the db host to %s\n", host);
	cmyth_database_close(db);
	cmyth_release(db->db_host);
	db->db_host = cmyth_strdup(host);
	if(! db->db_host)
	    return 0;
	else
	    return 1;
}

int
cmyth_database_set_user(cmyth_database_t db, char *user)
{
	PRINTF("** SSDEBUG: setting the db user to %s\n", user);
	cmyth_database_close(db);
	cmyth_release(db->db_user);
	db->db_user = cmyth_strdup(user);
	if(! db->db_user)
	    return 0;
	else
	    return 1;
}

int
cmyth_database_set_pass(cmyth_database_t db, char *pass)
{
	PRINTF("** SSDEBUG: setting the db pass to %s\n", pass);
	cmyth_database_close(db);
	cmyth_release(db->db_user);
	db->db_pass = cmyth_strdup(pass);
	if(! db->db_pass)
	    return 0;
	else
	    return 1;
}

int
cmyth_database_set_name(cmyth_database_t db, char *name)
{
	PRINTF("** SSDEBUG: setting the db name to %s\n", name);
	cmyth_database_close(db);
	cmyth_release(db->db_name);
	db->db_name = cmyth_strdup(name);
	if(! db->db_name)
	    return 0;
	else
	    return 1;
}

int
cmyth_db_check_connection(cmyth_database_t db)
{
    int new_conn = 0;
    if(db->mysql != NULL)
    {
	/* Fetch the mysql stats (uptime and stuff) to check the connection is
	 * still good
	 */
	if(mysql_stat(db->mysql) == NULL)
	{
	    cmyth_database_close(db);
	}
    }
    if(db->mysql == NULL)
    {
	db->mysql = mysql_init(NULL);
	new_conn = 1;
	if(db->mysql == NULL)
	{
	    fprintf(stderr,"%s: mysql_init() failed, insufficient memory?",
		    __FUNCTION__);
	    return -1;
	}
	if(NULL == mysql_real_connect(db->mysql,
		    db->db_host,db->db_user,db->db_pass,db->db_name,0,NULL,0))
	{
	    fprintf(stderr,"%s: mysql_connect() failed: %s", __FUNCTION__,
		    mysql_error(db->mysql));
	    cmyth_database_close(db);
	    return -1;
	}
    }
    return 0;
}

int 
cmyth_schedule_recording(cmyth_conn_t conn, char * msg)
{
	int err=0;
	int count;
	char buf[256];

	fprintf (stderr, "In function : %s\n",__FUNCTION__);
	if (!conn) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n", __FUNCTION__);
		return -1;
	}

	pthread_mutex_lock(&mutex);

	if ((err = cmyth_send_message(conn, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
                        "%s: cmyth_send_message() failed (%d)\n",__FUNCTION__,err);
		return err;
	}

	count = cmyth_rcv_length(conn);
	cmyth_rcv_string(conn, &err, buf, sizeof(buf)-1,count);
	pthread_mutex_unlock(&mutex);
	return err;
}

char *
cmyth_mysql_escape_chars(cmyth_database_t db, char * string) 
{
	char *N_string;
	size_t len;

	if(cmyth_db_check_connection(db) != 0)
	{
               cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_db_check_connection failed\n",
                           __FUNCTION__);
               fprintf(stderr,"%s: cmyth_db_check_connection failed\n", __FUNCTION__);
	       return NULL;
	}

	len = strlen(string);
	N_string=cmyth_allocate(len*2+1);
	mysql_real_escape_string(db->mysql,N_string,string,len); 

	return (N_string);
}

int
cmyth_mysql_insert_into_record(cmyth_database_t db, char * query, char * query1, char * query2, char *title, char * subtitle, char * description, char * callsign)
{
	int rows=0;
	char *N_title;
	char *N_subtitle;
	char *N_description;
	char *N_callsign;
	char N_query[2500];

	if(cmyth_db_check_connection(db) != 0)
	{
               cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_db_check_connection failed\n",
                           __FUNCTION__);
               fprintf(stderr,"%s: cmyth_db_check_connection failed\n", __FUNCTION__);
	       return -1;
	}

	N_title = cmyth_allocate(strlen(title)*2+1);
	mysql_real_escape_string(db->mysql,N_title,title,strlen(title)); 
	N_subtitle = cmyth_allocate(strlen(subtitle)*2+1);
	mysql_real_escape_string(db->mysql,N_subtitle,subtitle,strlen(subtitle)); 
	N_description = cmyth_allocate(strlen(description)*2+1);
	mysql_real_escape_string(db->mysql,N_description,description,strlen(description)); 
	N_callsign = cmyth_allocate(strlen(callsign)*2+1);
	mysql_real_escape_string(db->mysql,N_callsign,callsign,strlen(callsign)); 

	snprintf(N_query,2500,"%s '%s','%s','%s' %s '%s' %s",query,N_title,N_subtitle,N_description,query1,N_callsign,query2); 
	cmyth_release(N_title);
	cmyth_release(N_subtitle);
	cmyth_release(N_callsign);

/*
	fprintf (stderr, "\n\n\n\n");
	fprintf (stderr, "query = %s\n",query);
	fprintf (stderr, "N_title = %s\n",N_title);
	fprintf (stderr, "N_subtitle = %s\n",N_subtitle);
	fprintf (stderr, "N_description = %s\n",N_description);
	fprintf (stderr, "query1 = %s\n",query1);
	fprintf (stderr, "N_callsign=%s\n",N_callsign);
	fprintf (stderr, "query2 = %s\n",query2);
	fprintf (stderr, "N_query = %s\n",N_query);
*/

        if(mysql_real_query(db->mysql,N_query,(unsigned int) strlen(N_query))) {
                cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                           __FUNCTION__, mysql_error(db->mysql));
		return -1;
	}
	rows=mysql_insert_id(db->mysql);

	if (rows <=0) {
        	cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                	__FUNCTION__, mysql_error(db->mysql));
	}


	return rows;
}

int
cmyth_mysql_get_guide(cmyth_database_t db, cmyth_program_t **prog, time_t starttime, time_t endtime) 
{
	MYSQL_STMT *stmt = NULL;
	MYSQL_RES *prepare_meta_result;
	static MYSQL_BIND bind_params[4];
	static MYSQL_BIND bind_results[13];
	static my_bool firsttime = 1;
	static my_bool res_is_null[13];
	static my_bool res_error[13];
	static unsigned long res_length[13];
        const char *query = "SELECT program.chanid,UNIX_TIMESTAMP(program.starttime),UNIX_TIMESTAMP(program.endtime),program.title,program.description,program.subtitle,program.programid,program.seriesid,program.category,channel.channum,channel.callsign,channel.name,channel.sourceid FROM program LEFT JOIN channel on program.chanid=channel.chanid WHERE ( starttime>=FROM_UNIXTIME(?) and starttime<FROM_UNIXTIME(?) ) OR ( starttime <FROM_UNIXTIME(?) and endtime > FROM_UNIXTIME(?)) ORDER BY CAST(channel.channum AS UNSIGNED), program.starttime ASC ";
	int rows=0;
	int n=0;
	int done = 0;
	int result;
	if(firsttime)
	{
	    int i;
	    for(i = 0; i < sizeof(bind_params)/sizeof(*bind_params);i++)
	    {
		bind_params[i].buffer_type = MYSQL_TYPE_LONG;
		bind_params[i].is_null = 0;
		bind_params[i].length = 0;
	    }
	    for(i = 0; i < sizeof(bind_results)/sizeof(*bind_results);i++)
	    {
		bind_results[i].is_null = &res_is_null[i];
		bind_results[i].error = &res_error[i];
		bind_results[i].length = &res_length[i];
	    }
	    bind_results[0].buffer_type = MYSQL_TYPE_LONG;
	    bind_results[1].buffer_type = MYSQL_TYPE_LONG;
	    bind_results[2].buffer_type = MYSQL_TYPE_LONG;
	    bind_results[3].buffer_type = MYSQL_TYPE_STRING;
	    bind_results[3].buffer_length = sizeof((*prog)[0].title);
	    bind_results[4].buffer_type = MYSQL_TYPE_STRING;
	    bind_results[4].buffer_length = sizeof((*prog)[0].description);
	    bind_results[5].buffer_type = MYSQL_TYPE_STRING;
	    bind_results[5].buffer_length = sizeof((*prog)[0].subtitle);
	    bind_results[6].buffer_type = MYSQL_TYPE_STRING;
	    bind_results[6].buffer_length = sizeof((*prog)[0].programid);
	    bind_results[7].buffer_type = MYSQL_TYPE_STRING;
	    bind_results[7].buffer_length = sizeof((*prog)[0].seriesid);
	    bind_results[8].buffer_type = MYSQL_TYPE_STRING;
	    bind_results[8].buffer_length = sizeof((*prog)[0].category);
	    bind_results[9].buffer_type = MYSQL_TYPE_LONG;
	    bind_results[10].buffer_type = MYSQL_TYPE_STRING;
	    bind_results[10].buffer_length = sizeof((*prog)[0].callsign);
	    bind_results[11].buffer_type = MYSQL_TYPE_STRING;
	    bind_results[11].buffer_length = sizeof((*prog)[0].name);
	    bind_results[12].buffer_type = MYSQL_TYPE_LONG;
	    firsttime = 0;
	}

	if(cmyth_db_check_connection(db) != 0)
	{
               cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_db_check_connection failed\n",
                           __FUNCTION__);
               fprintf(stderr,"%s: cmyth_db_check_connection failed\n", __FUNCTION__);
	       return -1;
	}

	stmt=mysql_stmt_init(db->mysql);
	if(!stmt)
	{
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_stmt_init(), out of memory\n",
                           __FUNCTION__);
		fprintf(stderr, "mysql_stmt_init(),out of memory");
		return -1;
	}
	if(mysql_stmt_prepare(stmt,query,strlen(query)))
	{
	    cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_stmt_prepare(), Failed: %s\n",
				       __FUNCTION__, mysql_stmt_error(stmt));

	    fprintf(stderr, " mysql_stmt_prepare(), SELECT failed\n");
	    fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
	    mysql_stmt_close(stmt);
	    return -1;
	}
	if(mysql_stmt_param_count(stmt) != sizeof(bind_params)/sizeof(*bind_params))
	{
	    fprintf(stderr,"%s: Awooga! MySQL expects a different number of parameters to a statement to what we expected\n",__FUNCTION__);
	    exit(1);
	}
	bind_params[0].buffer = (char *) &starttime;
	bind_params[1].buffer = (char *) &endtime;
	bind_params[2].buffer = (char *) &starttime;
	bind_params[3].buffer = (char *) &starttime;
	if(mysql_stmt_bind_param(stmt,bind_params))
	{
	    fprintf(stderr,"%s, mysql_stmt_bind_param() failed: %s\n",
			    	__FUNCTION__, mysql_stmt_error(stmt));
	    mysql_stmt_close(stmt);
	    return -1;
	}

	/* Fetch result set meta information */
	prepare_meta_result = mysql_stmt_result_metadata(stmt);
	if (!prepare_meta_result)
	{
	    fprintf(stderr,
	     " mysql_stmt_result_metadata(), returned no meta information\n");
	    fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
	    mysql_stmt_close(stmt);
	    return -1;
	}

	/* Get total columns in the query */
	if(mysql_num_fields(prepare_meta_result) != sizeof(bind_results)/sizeof(*bind_results))/* validate column count */
	{
	      fprintf(stderr, "%s, invalid column count returned by MySQL\n",
		      	__FUNCTION__);
	      exit(1);
	}

	/* Execute the SELECT query */
	if (mysql_stmt_execute(stmt))
	{
	    fprintf(stderr, "%s: mysql_stmt_execute(), failed\n", __FUNCTION__);
	    fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
	    mysql_stmt_close(stmt);
	    return -1;
	}

	while(rows == 0 || !done) {
        	if (rows >= n) {
                	n+=10;
                       	*prog=realloc(*prog,sizeof(**prog)*(n));
               	}
		bind_results[0].buffer = (char *)&((*prog)[rows].chanid);
           	cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].chanid =  %d\n",rows, (*prog)[rows].chanid);
               	(*prog)[rows].recording=0;
		bind_results[1].buffer = (char *)&((*prog)[rows].starttime);
		bind_results[2].buffer = (char *)&((*prog)[rows].endtime);
		bind_results[3].buffer = &((*prog)[rows].title);
		bind_results[4].buffer = &((*prog)[rows].description);
		bind_results[5].buffer = &((*prog)[rows].subtitle);
		bind_results[6].buffer = &((*prog)[rows].programid);
		bind_results[7].buffer = &((*prog)[rows].seriesid);
		bind_results[8].buffer = &((*prog)[rows].category);
		bind_results[9].buffer = (char *)&((*prog)[rows].channum);
		bind_results[10].buffer = &((*prog)[rows].callsign);
		bind_results[11].buffer = &((*prog)[rows].name);
		bind_results[12].buffer = (char *)&((*prog)[rows].sourceid);
		if(mysql_stmt_bind_result(stmt,bind_results))
		{
		    fprintf(stderr,"%s, mysql_stmt_bind_result() failed: %s\n",
			    	__FUNCTION__, mysql_stmt_error(stmt));
		}
		result = mysql_stmt_fetch(stmt);
		if(result == 0 || result== MYSQL_DATA_TRUNCATED)
		{
		    cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].chanid =  %ld\n",rows, (*prog)[rows].chanid);
		    cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].title =  %s\n",rows, (*prog)[rows].title);
		}
		else 
		{
		    if (result != MYSQL_NO_DATA)
		    {
			fprintf(stderr,"%s, mysql_stmt_fetch() failed: %s\n",
			    	__FUNCTION__, mysql_stmt_error(stmt));
		    }
		    done = 1;
		    rows--;
		}
          	rows++;
        }
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        cmyth_dbg(CMYTH_DBG_ERROR, "%s: rows= %d\n", __FUNCTION__, rows);
	return rows;
}

int
cmyth_mysql_get_prog_finder_char_title(cmyth_database_t db, cmyth_program_t **prog, time_t starttime, char *program_name) 
{
	MYSQL_RES *res=NULL;
	MYSQL_ROW row;
        char query[350];
	int rows=0;
	int n = 50;

	if(cmyth_db_check_connection(db) != 0)
	{
               cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_db_check_connection failed\n",
                           __FUNCTION__);
               fprintf(stderr,"%s: cmyth_db_check_connection failed\n", __FUNCTION__);
	       return -1;
	}

        if (strncmp(program_name, "@", 1)==0)
                snprintf(query, 350, "SELECT DISTINCT title FROM program " \
                                "WHERE ( title NOT REGEXP '^[A-Z0-9]' AND " \
				"title NOT REGEXP '^The [A-Z0-9]' AND " \
				"title NOT REGEXP '^A [A-Z0-9]' AND " \
				"starttime >= FROM_UNIXTIME(%d)) ORDER BY title", \
			(int)starttime);
        else
	        snprintf(query, 350, "SELECT DISTINCT title FROM program " \
					"where starttime >= FROM_UNIXTIME(%d) and " \
					"title like '%s%%' ORDER BY `title` ASC",
			 (int)starttime, program_name);

	fprintf(stderr, "%s\n", query);
        cmyth_dbg(CMYTH_DBG_ERROR, "%s: query= %s\n", __FUNCTION__, query);
        if(mysql_query(db->mysql,query)) {
                 cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                           __FUNCTION__, mysql_error(db->mysql));
		return -1;
        }
        res = mysql_store_result(db->mysql);
        while((row = mysql_fetch_row(res))) {
        	if (rows == n) {
                	n++;
                       	*prog=realloc(*prog,sizeof(**prog)*(n));
               	}
			sizeof_strncpy ( (*prog)[rows].title, row[0]);
        		cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].title =  %s\n",rows, (*prog)[rows].title);
			rows++;
        }
        mysql_free_result(res);
        cmyth_dbg(CMYTH_DBG_ERROR, "%s: rows= %d\n", __FUNCTION__, rows);
	return rows;
}

int
cmyth_mysql_get_prog_finder_time(cmyth_database_t db, cmyth_program_t **prog,  time_t starttime, char *program_name) 
{
	MYSQL_RES *res=NULL;
	MYSQL_ROW row;
        char query[630];
	char *N_title;
	int rows=0;
	int n = 50;
	int ch;


	if(cmyth_db_check_connection(db) != 0)
	{
               cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_db_check_connection failed\n",
                           __FUNCTION__);
               fprintf(stderr,"%s: cmyth_db_check_connection failed\n", __FUNCTION__);
	       return -1;
	}

	N_title = cmyth_allocate(strlen(program_name)*2+1);
	mysql_real_escape_string(db->mysql,N_title,program_name,strlen(program_name)); 

        //sprintf(query, "SELECT chanid,starttime,endtime,title,description,subtitle,programid,seriesid,category FROM program WHERE starttime >= '%s' and title ='%s' ORDER BY `starttime` ASC ", starttime, N_title);
        snprintf(query, 630, "SELECT program.chanid,UNIX_TIMESTAMP(program.starttime),UNIX_TIMESTAMP(program.endtime),program.title,program.description,program.subtitle,program.programid,program.seriesid,program.category, channel.channum, channel.callsign, channel.name, channel.sourceid FROM program LEFT JOIN channel on program.chanid=channel.chanid WHERE starttime >= FROM_UNIXTIME(%d) and title ='%s' ORDER BY `starttime` ASC ", (int)starttime, N_title);
	cmyth_release(N_title);
	fprintf(stderr, "%s\n", query);
        cmyth_dbg(CMYTH_DBG_ERROR, "%s: query= %s\n", __FUNCTION__, query);
        if(mysql_query(db->mysql,query)) {
                 cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                           __FUNCTION__, mysql_error(db->mysql));
		return -1;
       	}
	cmyth_dbg(CMYTH_DBG_ERROR, "n =  %d\n",n);
        res = mysql_store_result(db->mysql);
	cmyth_dbg(CMYTH_DBG_ERROR, "n =  %d\n",n);
	while((row = mysql_fetch_row(res))) {
			cmyth_dbg(CMYTH_DBG_ERROR, "n =  %d\n",n);
        	if (rows == n) {
                	n++;
			cmyth_dbg(CMYTH_DBG_ERROR, "realloc n =  %d\n",n);
                       	*prog=realloc(*prog,sizeof(**prog)*(n));
               	}
			cmyth_dbg(CMYTH_DBG_ERROR, "rows =  %d\nrow[0]=%d\n",rows, row[0]);
			cmyth_dbg(CMYTH_DBG_ERROR, "row[1]=%d\n",row[1]);
			ch = atoi(row[0]);
			(*prog)[rows].chanid=ch;
			cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].chanid =  %d\n",rows, (*prog)[rows].chanid);
			(*prog)[rows].recording=0;
			(*prog)[rows].starttime = atoi(row[1]);
			(*prog)[rows].endtime = atoi(row[2]);
			sizeof_strncpy ((*prog)[rows].title, row[3]);
			sizeof_strncpy ((*prog)[rows].description, row[4]);
			sizeof_strncpy ((*prog)[rows].subtitle, row[5]);
			sizeof_strncpy ((*prog)[rows].programid, row[6]);
			sizeof_strncpy ((*prog)[rows].seriesid, row[7]);
			sizeof_strncpy ((*prog)[rows].category, row[8]);
			(*prog)[rows].channum = atoi (row[9]);
			sizeof_strncpy ((*prog)[rows].callsign,row[10]);
			sizeof_strncpy ((*prog)[rows].name,row[11]);
			(*prog)[rows].sourceid = atoi (row[12]);
        		cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].chanid =  %d\n",rows, (*prog)[rows].chanid);
        		cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].title =  %s\n",rows, (*prog)[rows].title);
			rows++;
        }
        mysql_free_result(res);
        cmyth_dbg(CMYTH_DBG_ERROR, "%s: rows= %d\n", __FUNCTION__, rows);
	return rows;
}

int 
fill_program_recording_status(cmyth_conn_t conn, char * msg)
{
	int err=0;
	fprintf (stderr, "In function : %s\n",__FUNCTION__);
	if (!conn) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n", __FUNCTION__);
		return -1;
	}
	if ((err = cmyth_send_message(conn, msg)) < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR,
                        "%s: cmyth_send_message() failed (%d)\n",__FUNCTION__,err);
		return err;
	}
	return err;
}

