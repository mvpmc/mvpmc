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

int
get_myth_version(cmyth_conn_t conn)
{
	int version=0;

	if (!conn) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n", __FUNCTION__);
		return -1;
	}
	version = conn->conn_version;
	return version;
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

/*	fprintf (stderr, "MIKE : COUNT =%d\n",count);
	fprintf (stderr, "MIKE : err =%d\n",err);
	fprintf (stderr, "MIKE : BUF =%s\n",buf); */
	
	pthread_mutex_unlock(&mutex);

	return err;
}
char *
mysql_escape_chars(char * string, char *dbhost, char *dbuser, char *dbpass, char *db) 
{
	MYSQL mysql;
	char *N_string;
	unsigned int len;

	mysql_init(&mysql);
	if(!(mysql_real_connect(&mysql,dbhost,dbuser,dbpass,db,0,NULL,0))) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_connect() Failed: %s\n",
                           __FUNCTION__, mysql_error(&mysql));
		fprintf(stderr,"mysql_connect() Failed: %s\n", mysql_error(&mysql));
        	mysql_close(&mysql);
		goto out;
        }

	len = strlen(string);
	N_string=malloc(len * sizeof(char));
	strcpy(N_string,string);
	mysql_real_escape_string(&mysql,N_string,string,strlen(string)); 

	mysql_close(&mysql);
	return (N_string);

	out:        
		return (NULL);
		mysql_close(&mysql);
}

int
insert_into_record_mysql(char * query, char * query1, char * query2, char *title, char * subtitle, char * description, char * callsign, char *dbhost, char *dbuser, char *dbpass, char *db)
{
	MYSQL mysql;
	int rows=0;
	char N_title[260];
	char N_subtitle[260];
	char N_description[500];
	char N_callsign[25];
	char N_query[2500];

fprintf (stderr, "In function %s\n",__FUNCTION__);

	mysql_init(&mysql);

	if(!(mysql_real_connect(&mysql,dbhost,dbuser,dbpass,db,0,NULL,0))) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_connect() Failed: %s\n",
                           __FUNCTION__, mysql_error(&mysql));
		fprintf(stderr,"mysql_connect() Failed: %s\n", mysql_error(&mysql));
        	mysql_close(&mysql);
		return -1;
        }
	strcpy(N_title,title);
	mysql_real_escape_string(&mysql,N_title,title,strlen(title)); 
	strcpy(N_subtitle,subtitle);
	mysql_real_escape_string(&mysql,N_subtitle,subtitle,strlen(subtitle)); 
	strcpy(N_description,description);
	mysql_real_escape_string(&mysql,N_description,description,strlen(description)); 
	strcpy(N_callsign,callsign);
	mysql_real_escape_string(&mysql,N_callsign,callsign,strlen(callsign)); 

	sprintf(N_query,"%s '%s','%s','%s' %s '%s' %s",query,N_title,N_subtitle,N_description,query1,N_callsign,query2); 

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

        if(mysql_real_query(&mysql,N_query,(unsigned int) strlen(N_query))) {
                cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                           __FUNCTION__, mysql_error(&mysql));
        	mysql_close(&mysql);
		return -1;
	}
	rows=mysql_insert_id(&mysql);

	if (rows <=0) {
        	cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                	__FUNCTION__, mysql_error(&mysql));
	}


        mysql_close(&mysql);
	return rows;
}

int
get_guide_mysql(struct program *prog, struct channel *chan, char *starttime, char *endtime, char *dbhost, char *dbuser, char *dbpass, char *db) 
{
	MYSQL *mysql;
	MYSQL_RES *res=NULL;
	MYSQL_ROW row;
        char query[350];
	int rows=2;
	int ch;

        mysql=mysql_init(NULL);
	if(!(mysql_real_connect(mysql,dbhost,dbuser,dbpass,db,0,NULL,0))) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_connect() Failed: %s\n",
                           __FUNCTION__, mysql_error(mysql));
		fprintf(stderr, "mysql_connect() Failed: %s\n",mysql_error(mysql));
        	mysql_close(mysql);
		return -1;
        }
        sprintf(query, "SELECT chanid,starttime,endtime,title,description,subtitle,programid,seriesid,category FROM program WHERE starttime>='%s' and starttime<'%s' ORDER BY `chanid` ASC ",starttime,endtime);
        cmyth_dbg(CMYTH_DBG_ERROR, "%s: query= %s\n", __FUNCTION__, query);
        if(mysql_query(mysql,query)) {
                 cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                           __FUNCTION__, mysql_error(mysql));
        	mysql_close(mysql);
		return -1;
        }
        res = mysql_store_result(mysql);
	strcpy (prog[0].starttime, starttime);
	strcpy (prog[0].endtime, endtime);
        while((row = mysql_fetch_row(res))) {
		if (rows < 650) {
			ch = atoi(row[0]);
			prog[rows].channum = chan[ch].channum;
			prog[rows].chanid=ch;
			prog[rows].recording=0;
			strcpy ( prog[rows].starttime, row[1]);
			strcpy ( prog[rows].endtime, row[2]);
			strcpy ( prog[rows].title, row[3]);
			strcpy ( prog[rows].description, row[4]);
			strcpy ( prog[rows].subtitle, row[5]);
			strcpy ( prog[rows].programid, row[6]);
			strcpy ( prog[rows].seriesid, row[7]);
			strcpy ( prog[rows].category, row[8]);
        		cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].chanid =  %d\n",rows, prog[rows].chanid);
        		cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].title =  %s\n",rows, prog[rows].title);
			rows++;
		}
		else {
			fprintf (stderr, "structure full, too many listings : %s",__FUNCTION__ );
		}
        }
        mysql_free_result(res);
        mysql_close(mysql);
        cmyth_dbg(CMYTH_DBG_ERROR, "%s: rows= %d\n", __FUNCTION__, rows);
	return rows;
}

int
get_prog_finder_char_title_mysql(struct program *prog, char *starttime, char *program_name, char *dbhost, char *dbuser, char *dbpass, char *db) 
{
	MYSQL *mysql;
	MYSQL_RES *res=NULL;
	MYSQL_ROW row;
        char query[350];
	int rows=0;

        mysql=mysql_init(NULL);
	if(!(mysql_real_connect(mysql,dbhost,dbuser,dbpass,db,0,NULL,0))) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_connect() Failed: %s\n",
                           __FUNCTION__, mysql_error(mysql));
		fprintf(stderr, "mysql_connect() Failed: %s\n",mysql_error(mysql));
        	mysql_close(mysql);
		return -1;
        }

        sprintf(query, "SELECT DISTINCT title FROM program where starttime >= '%s' and title like '%s%%' ORDER BY `title` ASC", starttime, program_name);
	fprintf(stderr, "%s\n", query);
        cmyth_dbg(CMYTH_DBG_ERROR, "%s: query= %s\n", __FUNCTION__, query);
        if(mysql_query(mysql,query)) {
                 cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                           __FUNCTION__, mysql_error(mysql));
        	mysql_close(mysql);
		return -1;
        }
        res = mysql_store_result(mysql);
        while((row = mysql_fetch_row(res))) {
		if (rows < 650) {
			strcpy ( prog[rows].title, row[0]);
        		cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].title =  %s\n",rows, prog[rows].title);
			rows++;
		}
		else {
			fprintf (stderr, "structure full, too many listings : %s",__FUNCTION__ );
		}
        }
        mysql_free_result(res);
        mysql_close(mysql);
        cmyth_dbg(CMYTH_DBG_ERROR, "%s: rows= %d\n", __FUNCTION__, rows);
	return rows;
}

int
get_prog_finder_time_mysql(struct program *prog,  char *starttime, char *program_name, char *dbhost, char *dbuser, char *dbpass, char *db) 
{
	MYSQL mysql;
	MYSQL_RES *res=NULL;
	MYSQL_ROW row;
        char query[350];
	char N_title[260];
	int rows=0;
	int ch;


	mysql_init(&mysql);

	if(!(mysql_real_connect(&mysql,dbhost,dbuser,dbpass,db,0,NULL,0))) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_connect() Failed: %s\n",
                           __FUNCTION__, mysql_error(&mysql));
		fprintf(stderr, "mysql_connect() Failed: %s\n",mysql_error(&mysql));
        	mysql_close(&mysql);
		return -1;
        }

	strcpy(N_title,program_name);
	mysql_real_escape_string(&mysql,N_title,program_name,strlen(program_name)); 

        sprintf(query, "SELECT chanid,starttime,endtime,title,description,subtitle,programid,seriesid,category FROM program WHERE starttime >= '%s' and title ='%s' ORDER BY `starttime` ASC ", starttime, N_title);
	fprintf(stderr, "%s\n", query);
        cmyth_dbg(CMYTH_DBG_ERROR, "%s: query= %s\n", __FUNCTION__, query);
        if(mysql_query(&mysql,query)) {
                 cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                           __FUNCTION__, mysql_error(&mysql));
        	mysql_close(&mysql);
		return -1;
        }
        res = mysql_store_result(&mysql);
        while((row = mysql_fetch_row(res))) {
		if (rows < 650) {
			ch = atoi(row[0]);
			prog[rows].chanid=ch;
			prog[rows].recording=0;
			strcpy ( prog[rows].starttime, row[1]);
			strcpy ( prog[rows].endtime, row[2]);
			strcpy ( prog[rows].title, row[3]);
			strcpy ( prog[rows].description, row[4]);
			strcpy ( prog[rows].subtitle, row[5]);
			strcpy ( prog[rows].programid, row[6]);
			strcpy ( prog[rows].seriesid, row[7]);
			strcpy ( prog[rows].category, row[8]);
        		cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].chanid =  %d\n",rows, prog[rows].chanid);
        		cmyth_dbg(CMYTH_DBG_ERROR, "prog[%d].title =  %s\n",rows, prog[rows].title);
			rows++;
		}
		else {
			fprintf (stderr, "structure full, too many listings : %s",__FUNCTION__ );
		}
        }
        mysql_free_result(res);
        mysql_close(&mysql);
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

int
myth_load_channels(struct channel *chan, char *dbhost, char *dbuser, char *dbpass, char *db) 
{
	MYSQL *mysql;
	MYSQL_RES *res=NULL;
	MYSQL_ROW row;
        char query[100];
	int rows=0;
	int key;
        
	mysql=mysql_init(NULL);
	if(!(mysql_real_connect(mysql,dbhost,dbuser,dbpass,db,0,NULL,0))) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_connect() Failed: %s\n",
                           __FUNCTION__, mysql_error(mysql));
		fprintf(stderr, "mysql_connect() Failed: %s\n",mysql_error(mysql));
        	mysql_close(mysql);
		return -1;
        }
        sprintf(query, "SELECT chanid,channum,callsign,name FROM channel ORDER BY `chanid` ASC");
        cmyth_dbg(CMYTH_DBG_ERROR, "%s: query= %s\n", __FUNCTION__, query);

        if(mysql_query(mysql,query)) {
                 cmyth_dbg(CMYTH_DBG_ERROR, "%s: mysql_query() Failed: %s\n", 
                           __FUNCTION__, mysql_error(mysql));
        	mysql_close(mysql);
		return -1;
        }
        res = mysql_store_result(mysql);
        while((row = mysql_fetch_row(res))) {
		if (rows < 500) {
			key = atoi(row[0]);
			chan[key].chanid = key;
			chan[key].channum = atoi(row[1]);
			strcpy ( chan[key].callsign, row[2]);
			strcpy ( chan[key].name, row[3]);
        		cmyth_dbg(CMYTH_DBG_ERROR, "chan[%d].channum =  %d\n",key, chan[key].channum);
        		cmyth_dbg(CMYTH_DBG_ERROR, "chan[%d].name =  %s\n",key, chan[key].name);
			rows++;
		}
        }
        mysql_free_result(res);
        mysql_close(mysql);
       	cmyth_dbg(CMYTH_DBG_ERROR, "%s returned rows =  %d\n",__FUNCTION__, rows);
	return rows;
}
